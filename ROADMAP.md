# c-RCP Roadmap

## Protocol Replacement Notice — read this first

A gap analysis (2026-07-27/28) confirmed that everything below "Legacy Release
Plan" and "Legacy Milestones" describes a working but **entirely different
wire protocol** from the real industry standard this project was always meant
to track. c-RCP's existing Zone/Command/Response/Status model over a bespoke
16-byte header shares nothing at the wire level with the OPEN Alliance TC18
Remote Control Protocol (the actual "RCP" this project is named after).

The user has explicitly authorized a **full replacement**, not an
incremental gap-patch: c-RCP's core protocol is going to *become* the real
TC18 protocol. Phases 13–22 below are that replacement program. Everything
in Phase 1–12 (the "Legacy" sections, milestones 1–58) is **superseded**,
not extended — it remains in this file as project history (and because
several of its satellite packages carry forward, per the disposition table
below), not as a description of where this project is headed.

**This breaks every existing consumer of this library.** `rcp_zone_t`,
`rcp_command_t`/`rcp_response_t`/`rcp_status_t`, `rcp_controller_t`,
`rcp_registry_t`, and the wire format in `wire.c` all go away. Every one of
the ~30 satellite packages built as a generic decorator around
`rcp_controller_t`'s `send(cmd) -> response` shape (authz, ratelimit, loan,
observe, faultinject, prioqueue, redundancy, proxy, zonegroup, federation,
recorder, tsn, e2e, watchdog, deadline, powerstate, sim, ...) loses the
single generic "Command" abstraction it was built against, because TC18 has
no such abstraction — it has 13 heterogeneous endpoint types, each with its
own fixed request/response shape, sitting behind a register-map
configuration model. This is not a header-compatible refactor.

**No compatibility shim is planned.** Reasoned explicitly, not assumed:
1. The wire format changes at the byte level (IEEE 1722 AVTPDU/ACF framing
   vs. this project's custom length-prefixed frames) — no in-process shim
   makes an old client and a new server (or vice versa) interoperate; that
   would require a protocol translator no simpler than one of the ADAPT-class
   bridges below, and speaking two incompatible wire protocols out of one
   `send()` call is not a "compatibility" story, it's a second protocol
   stack.
2. The semantic model changes too — there is no TC18 concept of a
   zone-addressed, priority-tagged, generically-typed "Command"; addressing
   is server/endpoint/`(stream_id, byte_bus_id)`, and payload shape is fixed
   per endpoint type. A shim would have to invent a Zone/Command
   abstraction TC18 itself doesn't have, which defeats the point of
   conforming to it.
3. c-RCP is pre-1.0 (`0.x` throughout its history) — semver already signals
   "anything may change at any time," and the user's authorization is
   explicit. Anyone needing the old behavior can pin to the last `v0.58.x`
   tag; that tag remains a valid, buildable snapshot of the informal
   protocol indefinitely. That's the soft landing, not a maintained shim.
4. If cross-version interop is later found to matter operationally
   (unlikely — this predecessor protocol has never actually implemented the
   thing it was named after), a standalone bridge package following the
   same ADAPT pattern as `canbr`/`linbr` is the right shape for it, not a
   shim baked into the core.

**This project stops mirroring cpp-RCP port-for-port.** Every milestone in
Phase 1–12 below is phrased as "ports cpp-RCP's `X.hpp`" — that convention
ends at Phase 13. cpp-RCP (and go-RCP/rust-RCP) are out of scope for this
effort entirely; nothing here assumes they are, or aren't, making the same
jump. From Phase 13 onward, requirements are derived directly from the OPEN
Alliance TC18 Remote Control Protocol Specification v0.5.1_RC, via a
structured internal gap-analysis extraction prepared for this effort
(cited below by its own section numbers, e.g. "extraction §3.2", for
traceability — that extraction document itself is a confidential working
reference and is never to be committed to this or any repository; only the
spec's own chapter numbers, defined field names, and numeric constants —
protocol facts, not the spec's prose — appear in this roadmap and in any
code/comments that implement it).

---

## Satellite Package Disposition

Every package alongside the core protocol (`include/rcp/*.h` + `src/*.c`,
excluding the core protocol/codec/RELAY-binding surface itself) gets an
individual, justified call. Four dispositions, matching CONTRIBUTING's
process for tracking scope decisions:

- **REPLACE** — becomes a spec-conformant equivalent of a concept the spec
  itself defines (possibly under a new name/shape); the old implementation
  is not salvageable as-is.
- **ADAPT** — sits on top of / integrates with the new core protocol;
  needs its calls rebound to the new request/response primitive, not a
  redesign of its own purpose or architecture.
- **DEPRECATE** — no place in the new model; removed (or archived,
  unmaintained) rather than carried forward.
- **KEEP AS-IS** — genuinely orthogonal to the protocol replacement,
  unaffected.

| Package | Disposition | Reason |
|---|---|---|
| `rcp.h`/`rcp.c` | **REPLACE** (core, not satellite) | The Zone/Command/Response/Status/Controller/Registry types *are* the thing being replaced — see Phase 13–14. |
| `wire.h`/`wire.c` | **REPLACE** (core, not satellite) | Custom length-framed codec replaced wholesale by IEEE 1722 AVTPDU/ACF framing — Phase 13. |
| `relay/relay.h`, `adapt.h`/`adapt.c` | **REPLACE**, flagged for upstream coordination | RELAY's generic `Message`/`ToMessage()`/`FromMessage()` envelope assumes a single generic command/response shape; TC18's 13 heterogeneous, fixed-shape endpoint types don't map onto that envelope without per-endpoint-type translation rules RELAY itself doesn't yet define. Tracked at Phase 21 (M84) as its own milestone rather than assumed solvable as a drop-in port. |
| `config.h`/`config.c` | **REPLACE** | Zone-manifest JSON schema has no TC18 counterpart; becomes an RC-Server/endpoint manifest (HW pin map, EP list, stream config) instead. |
| `cli.h`/`cli.c`, `cli/main.c` | **ADAPT** | The three RELAY-mandated commands (`version`/`capabilities`/`status`) stay; only `capabilities`'s *content* (currently the old `RCP_CMD_*` enum) needs updating to describe the new feature-bundle flags (`svr_implemented_options`). |
| `clock.h`/`clock.c`, `platform.h`/`platform.c` | **KEEP AS-IS** | OS-abstraction seam (mutex/cond/thread/clock) is orthogonal to wire format; may gain a gPTP time-source query later, but isn't rewritten. |
| `version.h` | **KEEP AS-IS** | Mechanical version string; bumped per milestone as always. |
| `mock.h`/`mock.c` | **REPLACE** | The entire vtable it doubles for (`rcp_controller_t`) is gone; becomes an in-process RC-Server/endpoint test double instead. |
| `sim.h`/`sim.c` | **REPLACE** | Same reasoning as `mock`; the SiL/HIL-simulator *purpose* is preserved (arguably more valuable given TC18's state-machine complexity) but the implementation is a full rewrite against the new endpoint/lifecycle model. |
| `udp.h`/`udp.c` | **REPLACE** | Becomes the IEEE 1722-over-UDP/IP (spec Annex J) transport carrying AVTPDUs; POSIX socket/thread plumbing may be reused as a starting point, but the frame codec it carries fully changes. |
| `shmem.h`/`shmem.c` | **REPLACE** | Rebuilt as an in-process AVTPDU-frame loopback transport; likely consolidates with the new `mock` backend rather than staying a fully separate module. |
| `tls.h`/`tls.c` | **DEPRECATE** | Spec's actual security control is MACsec (802.1AE, link-layer, explicitly declared a product-specific/opaque config block) — a materially different mechanism to an application-level TLS session wrapper. No natural home in the new model; revisit only if the IEEE1722-over-UDP/IP transport variant later wants DTLS. |
| `watchdog.h`/`watchdog.c` | **REPLACE** | Superseded by the spec's own per-request-stream watchdog (`rx_wd_enable`/`rx_wd_timeout_interval`/`rx_wd_safestate_enable`) plus the safety-request (`0x8x`) mechanism (Phase 18); the "kick + health state machine" idea survives only as a thin client convenience around that register, not as an independent side-channel command. |
| `deadline.h`/`deadline.c` | **REPLACE** | No generic "Status stream" exists to monitor in TC18; liveness must be re-derived from response/ack-queue heartbeats (`Flush_time`) and/or `rx_wd_info_enable` safe-state notifications. |
| `powerstate.h`/`powerstate.c` | **REPLACE** | Ad-hoc Active/Sleeping/BusOff model replaced by the spec's Normal/StandBy/Sleep/Unpowered model with cold-start/hot-start distinction and the WakeUp handshake (Phase 19). |
| `e2e.h`/`e2e.c` | **REPLACE** | Ad-hoc CRC-16 + sequence + replay-guard replaced by the spec's actual CRC32 (poly `0xF4ACFB13`) "safe point" mechanism and safety-request MSB-tagged request variants (Phase 18). |
| `prioqueue.h`/`prioqueue.c` | **DEPRECATE** | Request execution priority is now a protocol-defined, server-side property of request *kind* (cancellation > triggered > timed > compound > compound-wait > chained > standard), not a client-assigned per-command priority queued before transmission. A client-side priority heap has no defined role to attach to. |
| `ratelimit.h`/`ratelimit.c` | **ADAPT** | Still a reasonable client-side self-throttling decorator against an endpoint's finite, un-flow-controlled request-queue capacity — needs rebinding to the new send primitive, not a redesign. |
| `authz.h`/`authz.c` | **ADAPT** | Access-control-before-send is protocol-agnostic; policy keys move from zone/command-type to stream/endpoint/request-type identifiers. |
| `firmware.h`/`firmware.c` | **DEPRECATE** | No OTA/firmware-update endpoint or message exists anywhere in the spec — it's explicitly an OEM/application-layer concern, out of scope of RCP itself. No protocol hook left to attach it to. |
| `zonegroup.h`/`zonegroup.c` | **DEPRECATE** | "Zone" as a first-class addressable/groupable unit doesn't exist in TC18; addressing is server/endpoint/`(stream_id, byte_bus_id)`, with no defined multi-target broadcast grouping concept. |
| `proxy.h`/`proxy.c` | **DEPRECATE** | No zone concept to proxy; the spec explicitly delegates multi-hop/bridging concerns to "the network," not the RC system. |
| `redundancy.h`/`redundancy.c` | **DEPRECATE** | Built entirely on Controller/Zone; an RC Server is a single node with one lifecycle state — no server-redundancy concept is defined. |
| `federation.h`/`federation.c` | **DEPRECATE** | The zone/HPC-lease registry model has no TC18 counterpart. |
| `observe.h`/`observe.c` | **ADAPT** | Latency-span/counter wrapping is protocol-agnostic; rebind around the new send primitive, with metric labels moving from zone/command-type to server/endpoint/request-type. |
| `admin.h`/`admin.c` | **ADAPT** | In-process zones/metrics/SSE admin surface reworked to list registered RC Servers/endpoints instead of zones; the SSE/metrics plumbing itself is unaffected. |
| `recorder.h`/`recorder.c` | **ADAPT** | Record/replay of raw ACF messages/AVTPDUs is a strictly more general, still-useful version of the same idea — needs a new on-the-wire capture format, not an architectural redesign. |
| `dyndata.h`/`dyndata.c` | **DEPRECATE** | Every TC18 endpoint type has one fixed, spec-defined payload shape; a client-negotiated schema registry solves a problem this protocol doesn't have. |
| `faultinject.h`/`faultinject.c` | **ADAPT** | Fault-injection-at-the-boundary (drop/slow/error/timeout) is protocol-agnostic; rebind to whatever new send primitive exists. |
| `loan.h`/`loan.c` | **ADAPT** | The pooled zero-copy buffer pattern is still valuable for large endpoint payloads (UART RX FIFO, CAN XL up to 2054B, SPI) — rebind to the new send primitive. |
| `tsn.h`/`tsn.c` | **ADAPT** | 802.1p PCP tagging over AVB/TSN Ethernet stays relevant, but its priority source must move from the retired `rcp_priority_t` enum to the protocol's own request-kind execution-priority classes (spec Ch.12 §3.14). |
| `mdns.h`/`mdns.c` | **ADAPT** | Retained as an *optional* convenience discovery layer for the IEEE1722-over-UDP/IP transport specifically — layered beside, never instead of, the spec's own mandatory native broadcast discovery (Phase 15). |
| `grpcbridge.h`/`.c`, `restbridge.h`/`.c`, `someipbr.h`/`.c`, `ddsbr.h`/`.c`, `mqttbr.h`/`.c`, `udsbr.h`/`.c`, `doipbr.h`/`.c` | **ADAPT** | All seven are compile-time stub decorators with no linked backend today; each just needs its framing calls updated to the new endpoint-request/response shape, not a redesign. |
| `canbr.h`/`canbr.c` | **ADAPT**, narrowed role | CAN becomes a *native* endpoint type (Phase 19 §5.11) **and** the spec allows CAN(FD/XL) as RCP's own underlying transport network (extraction §2.1) — this bridge's remaining, non-overlapping role is gatewaying to an *external* CAN segment not reachable via either of those two paths. Docs must draw this three-way distinction explicitly so nobody builds the same CAN support three times. |
| `linbr.h`/`linbr.c` | **ADAPT**, narrowed role | Same reasoning as `canbr`: LIN is now also a native endpoint type (Phase 19 §5.10); this bridge's role narrows to external-LIN-segment gatewaying. |
| Requirements/safety/security artifacts (`.fusa*.json`, `HARA.md`, `tara.md`/`.json`, `CYBERSECURITY.md`, `AUDIT_PACK.md`, `tla/*.tla`, `FORMAL_VERIFICATION.md`, `*-gap-report.json`, `coverage-report.json`, SBOM/SPDX/provenance files) | **REPLACE**, deliberately last | Every requirement, hazard, TARA entry, and TLA+ model here describes Zone/Command/Watchdog/CRC-16 behavior. All become stale once the protocol changes, but re-deriving certification evidence from code that doesn't exist yet is backwards — tracked as Phase 22, after implementation lands. |
| `SECURITY.md`, `INCIDENT-RESPONSE.md`, `.github/CODEOWNERS`, `CONTRIBUTING.md` | **KEEP AS-IS** | Process documents, protocol-agnostic. |
| `CMakeLists.txt`, `cmake/FetchDeps.cmake`, `tests/CMakeLists.txt` | **KEEP AS-IS** | Build tooling; will gain/lose targets as source files come and go, but the build system's own structure is unaffected. |
| `PORTABILITY.md` | **KEEP AS-IS**, revisit later | RTOS/bare-metal audit conclusions (single `platform.h` seam, no libc assumptions beyond C99) still hold structurally; worth a re-read once the new endpoint code exists, not a rewrite now. |
| `README.md` | Rewritten alongside Phase 13–14 | Not a "satellite package," but its Zone/Command wire-model description goes stale the moment Phase 13 lands; tracked as part of that work, not called out as its own milestone here. |

---

## Vision

c-RCP is a pure-C99 implementation of the OPEN Alliance TC18 Remote Control
Protocol (RCP) — the real automotive zonal-architecture standard this
project is named after, and, as of Phase 13, actually conforms to. It lets
a central/zonal ECU's application logic drive low-level peripheral
interfaces (SPI, GPIO, I²C, UART, ADC, PWM, LIN, CAN, ISELED, MDIO, ...)
physically wired to a separate, simpler ECU — the RC Server — over an
IEEE 1722-framed Ethernet (or CAN(FD/XL)) link, without that simpler ECU
needing any OEM-specific application logic of its own.

*(Historical note: through v0.58.0/Phase 1–12, this project instead
described itself as "a feature and API mirror of cpp-RCP" implementing an
informal, bespoke Zone/Command/Response/Status protocol. See the Protocol
Replacement Notice above for why that framing no longer applies and is not
being extended.)*

The project focuses on:

- Byte-for-byte conformance to the OPEN Alliance TC18 RCP wire protocol —
  correctness against the spec, not against a sibling repo's port
- Safety-first design, re-traced to ISO 26262 ASIL-B requirements once the
  new protocol's actual safety mechanisms (E2E CRC32 safe points, watchdog
  safe-state entry) exist to trace against (Phase 22)
- A small, dependency-free C99 core — vtable-based interfaces, swappable
  transports (native Ethernet, IEEE1722-over-UDP/IP, CAN(FD/XL)-as-network)
- Deterministic latency suitable for hard real-time automotive contexts
- Observability by default — metrics, heartbeats, and watchdog support
  built in, re-derived from the protocol's own heartbeat/safe-state
  mechanisms rather than a bespoke side channel

---

## Guiding Principles

1. Pure C99 first — no OS-specific headers in core interfaces
2. Safety as a first-class concern — requirements in `.fusa-reqs.json`,
   traced to tests (re-derived against the new protocol at Phase 22, not
   assumed to carry over from the superseded catalog)
3. Simplicity over completeness — clean interfaces, not a protocol kitchen
   sink
4. Testability by default — a mock RC-Server/endpoint backend ships with
   the library
5. **Spec-conformant, not zonal-architecture-native** — as of Phase 13,
   `rcp_zone_t` and the "zonal architecture" framing are retired; the RC
   Server/Endpoint/register-map model *is* the first-class type, because
   that's what the real TC18 spec defines. (This supersedes the prior
   "Zonal architecture native" principle from Phase 1–12 — see the
   Protocol Replacement Notice.)
6. Transport-agnostic — the spec itself requires this: native Ethernet,
   IEEE1722-over-UDP/IP, or CAN(FD/XL)-as-network are all valid RCP
   carriers, swappable without changing endpoint-facing API shape
7. Explicit ownership — every heap allocation has one clearly documented
   owner; no hidden allocation behind "value semantics" the way C++
   containers provide
8. Requirements derived from the spec, not mirrored from a sibling repo —
   see the Protocol Replacement Notice's note on cpp-RCP/go-RCP/rust-RCP

---

## Process

One PR per milestone below. Each PR must be CI-green (cross-platform build+test
matrix, DCO, and the full `cfusa` gate set) before merge, and tagged with the
milestone's version immediately after merge. See `CONTRIBUTING.md`.

Checkboxes below are updated as milestones land; this file is the durable
cross-session tracker for "what's implemented" and "what's next."

---

## Legacy Release Plan (v0.1.0–v0.58.0, superseded)

Kept as project history — see the Protocol Replacement Notice. Not
extended; the active plan is "TC18 Replacement Release Plan" below.

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

## TC18 Replacement Release Plan (v0.59.0+, active)

Dependency-ordered per the Protocol Replacement Notice. Versions are
projected, not committed — actual version numbers are assigned as each
milestone starts, same as every phase before it. Phases 13–20 sequence the
core protocol itself (in the order recommended by this effort's own
gap-analysis extraction, §9); Phase 21 works back through every satellite
package per its Disposition table entry above; Phase 22 re-derives
certification evidence against what Phase 13–21 actually built.

| Phase | Version | Theme | Summary |
|---|---|---|---|
| **Wire Format Core** | v0.59.0 | AVTPDU framing | NTSCF/TSCF headers, `stream_id`/`byte_bus_id` addressing, transport independence (Ethernet / IEEE1722-over-UDP-IP / CAN(FD/XL)-as-network) |
| **Wire Format Core** | v0.60.0 | ACF message format | ACF_ABB/ACF_GBB, the shared `byte_message_info` header, the four response types |
| **RC Server Lifecycle** | v0.61.0 | Lifecycle state machine | HW_UNCONFIGURED / HW_CONFIGURED / RCP_CONFIGURED, plausibility checks, register locking |
| **RC Server Lifecycle** | v0.62.0 | Register-map model | Generic vs. functional config split, EP0, root-client, general register map |
| **Discovery** | v0.63.0 | Discovery | Broadcast discovery read, discovery-stream claiming, `Discovery_TimeOut` |
| **Basic Endpoints** | v0.64.0 | GPIO endpoint | Simplest bitmask read/write endpoint, per-pin triggers |
| **Basic Endpoints** | v0.65.0 | SPI endpoint | Up to 6 channels, transfer-done/CS-edge triggers |
| **Basic Endpoints** | v0.66.0 | I²C + UART endpoints | Raw-byte-stream controller-only endpoints |
| **Basic Endpoints** | v0.67.0 | ADC + PWM_OUT + PWM_IN endpoints | Timing/averaging-flavored endpoints |
| **Conditional Requests** | v0.68.0 | Compound requests | Compound + compound-wait + sequencers |
| **Conditional Requests** | v0.69.0 | Triggered/chained/timed + cancellation | Full request-kind taxonomy, clear-all/non-safestate/single |
| **E2E Protection** | v0.70.0 | Safe points | CRC32 (`0xF4ACFB13`) safe-point mechanism, safety-request MSB variants |
| **Remaining Endpoints** | v0.71.0 | LIN commander endpoint | Raw-byte pass-through LIN master |
| **Remaining Endpoints** | v0.72.0 | CAN controller endpoint | Classical/FD/XL, data frames only |
| **Remaining Endpoints** | v0.73.0 | ISELED endpoint | LED/sensor daisy-chain, native encoding |
| **Remaining Endpoints** | v0.74.0 | MDIO endpoint | Clause-22/45 PHY management access |
| **Remaining Endpoints** | v0.75.0 | Wakeup control + power modes | Normal/StandBy/Sleep/Unpowered, cold/hot start, WakeUp handshake |
| **Fragmentation** | v0.76.0 | Fragmentation | Multi-AVTPDU `ms`/`segment_num` support for CAN XL, UART, large discovery reads |
| **Satellite Rework** | v0.77.0 | Foundational test/config satellites | REPLACE `mock`, `config`; ADAPT `cli` capabilities payload |
| **Satellite Rework** | v0.78.0 | Transport satellites | REPLACE `udp`, `shmem`; ADAPT `tsn`; DEPRECATE `tls` |
| **Satellite Rework** | v0.79.0 | Safety-adjacent satellites | REPLACE `watchdog`, `deadline`, `powerstate` |
| **Satellite Rework** | v0.80.0 | Generic decorators, batch 1 | ADAPT `authz`, `ratelimit`, `loan`, `observe`, `faultinject`, `admin`, `recorder` |
| **Satellite Rework** | v0.81.0 | Protocol bridges | ADAPT all 7 bridge stubs; narrow `canbr`/`linbr`'s role explicitly |
| **Satellite Rework** | v0.82.0 | Optional discovery convenience | ADAPT `mdns` |
| **Satellite Rework** | v0.83.0 | Deprecation batch | Remove `prioqueue`, `firmware`, `zonegroup`, `proxy`, `redundancy`, `federation`, `dyndata` |
| **Satellite Rework** | v0.84.0 | RELAY adapter rework | REPLACE `relay.h`/`adapt.c`, flagged for upstream RELAY-spec coordination |
| **Re-certification** | v0.85.0 | Safety/security re-certification | Requirements catalog, HARA, TARA, CYBERSECURITY.md, TLA+ specs, AUDIT_PACK.md, README.md rewritten against the new protocol |

---

## Legacy Milestones (v0.1.0–v0.58.0, superseded)

Kept as project history — see the Protocol Replacement Notice. Not
extended; new milestone detail starts at "TC18 Replacement Milestones"
after Phase 12 below.

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

### 17. Zone Simulator (v0.17.0) ✅

`include/rcp/sim.h` + `src/sim.c`: ports cpp-RCP's `sim.hpp` — a full
`rcp_controller_t` implementation (not a decorator, like `mock.h`) purpose-built
for SiL/HIL testing, adding configurable latency (constant or jitter via a
seeded `xorshift32` PRNG — C99 has no `<random>`, and MSVC lacks POSIX's
`rand_r()`, so a small self-contained generator avoids both), fault
injection (`rcp_sim_controller_fault()`/`_recover()`), periodic Status
publishing, and watchdog-miss detection to validate the safety mechanisms
from v0.11.0–v0.16.0 (watchdog, deadline, powerstate, e2e, prioqueue,
ratelimit).
- **Deviation note**: cpp-RCP's watchdog-miss detector runs a background
  thread that periodically refreshes a cached `wd_miss_` atomic bool. This
  port instead computes the miss state **on demand** in
  `rcp_sim_controller_watchdog_missed()` directly from the last-kick
  timestamp — always accurate, with no polling-interval staleness window.
  The background watchdog thread is still spawned (an otherwise-inert
  responsive sleep loop) so `close()` still has a second thread to join,
  preserving the "status and watchdog background threads" plural wording of
  REQ-SIM-008.
- The per-subscription watcher-thread pattern (auto-expire the returned
  channel on context timeout or controller close) and the publish/close
  subs-array-stealing technique are both reused verbatim from `mock.c`'s
  established design, applied to `sim.c`'s own struct.
- `tests/test_sim.c` ports all 8 of cpp-RCP's `test_sim.cpp` cases.
  Confirmed via 20 repeated local runs (debug + 20x ASan/UBSan) with no
  flakes.

### 18. Fault Injection (v0.18.0) ✅

`include/rcp/faultinject.h` + `src/faultinject.c`: ports cpp-RCP's
`faultinject.hpp` — a generic decorator (same vtable-wrapping pattern used
since v0.9.0) that intercepts `send()` according to an ordered list of
rules (`RCP_FI_DROP`/`_SLOW`/`_ERROR`/`_TIMEOUT`), each either firing
forever (`count = -1`) or auto-expiring after N firings.
- **Bug found and fixed, not reproduced**: cpp-RCP's own `pick_rule()`
  returns a raw `Rule*` into its internal `std::vector`, but calls
  `rules_.erase(it)` on that same element first whenever a count-based rule
  has just expired — the returned pointer (and the caller's subsequent
  `rule->type`/`rule->latency` reads in `send()`) is a genuine
  use-after-free in cpp-RCP's shipped code. This port's `fi_pick()` copies
  the rule's value out *before* deciding whether to remove it from the
  array, so the caller only ever reads its own local copy — the array
  mutation and the read are fully decoupled. (Scoped to this port's own
  `ROADMAP.md`/commit history per this project's process; no upstream
  cpp-RCP issue was filed, as that repository is out of scope for this
  session's instructions.)
- `tests/test_faultinject.c` ports all 8 of cpp-RCP's `test_faultinject.cpp`
  cases (passthrough, Drop, Error, Slow, count-based expiry, clear_rules,
  Timeout, zone passthrough).

---
### Phase 6 — Security
---

### 19. Authorization (v0.19.0) ✅

`include/rcp/authz.h` + `src/authz.c`: ports cpp-RCP's `authz.hpp` — a
generic decorator (same vtable-wrapping pattern used since v0.9.0) that
checks each `send()` against a shared, refcounted `rcp_authz_policy_t`
before forwarding, denying with the new `RCP_ERR_FORBIDDEN` error code
(added to the shared `rcp_errc_t` enum in `rcp.h`, alongside every other
generic controller error).
- `rcp_authz_policy_allow()` represents each entry's permitted
  zones/command-types as small `uint32_t` bitmasks (bit index = enum value)
  rather than cpp-RCP's `std::unordered_set`, avoiding per-entry heap
  allocation entirely — a mask of `0` naturally means "all", identical to
  cpp-RCP's "empty set = all" semantics, with no extra wildcard flag
  needed. Identity strings are fixed `RCP_AUTHZ_IDENTITY_MAX`-byte buffers
  (128 bytes) rather than arbitrary-length `std::string`, matching the
  fixed-buffer convention already established for short labels elsewhere
  in this port (e.g. `mdns.c`'s host/instance-name fields).
- `rcp_authz_policy_t` is refcounted (`_retain()`/`_release()`), matching
  cpp-RCP's `std::shared_ptr<AccessPolicy>` sharing guarantee across
  multiple `AuthController`s — a deliberate fidelity choice, since a plain
  non-owning pointer would diverge from cpp-RCP's actual lifetime contract.
- **Requirements catalog gap filled**: unlike every other milestone so far,
  `.fusa-reqs.json` had no forward-declared `REQ-AUTH-*` entries at all.
  Added `REQ-AUTH-001` through `-008` this milestone, matching the existing
  entries' schema (recorded under the same `iso26262`/ASIL-B fields as the
  rest of the safety catalog, even though authz's primary standard context
  is ISO 21434/IEC 62443 per cpp-RCP's own header comment — no separate
  cybersecurity-standard schema exists yet in this file).
- `tests/test_authz.c` ports all 10 of cpp-RCP's `test_authz.cpp` cases.
  Hit a new c-FuSa false-positive pattern: `CFUSA-CY009` (weak-crypto
  substring match on `des_`) flagged the ordinary English test-function
  name `test_identity_fn_overrides_set_identity`, whose middle
  ("overri**des_s**et") coincidentally contains that substring. Renamed to
  `test_identity_fn_takes_priority_over_fixed_identity`; proactively
  grepped the whole codebase for `des_` afterward, found no other
  occurrences.

### 20. Firmware Update / OTA (v0.20.0) ✅

`include/rcp/firmware.h` + `src/firmware.c`: ports cpp-RCP's `firmware.hpp`
— a multi-command OTA exchange state machine (Idle → Initiated →
Transferring → Verifying → Activated, with Rollback available from any
state), all steps sent as `RCP_CMD_UPDATE` (added to `rcp_command_type_t`
this milestone) with a 1-byte subcommand selector as the first payload
byte, matching cpp-RCP's own wire encoding exactly.
- **Scope note**: cpp-RCP's own header comment claims "SHA-256 integrity is
  computed over the full image before the Verify step," but no such
  computation exists anywhere in its shipped `FirmwareSession` — `verify()`
  just sends the bare subcommand byte with no hash. This port mirrors what
  cpp-RCP actually ships (no local hashing), not its more ambitious header
  narrative — the same judgment call made for UDP's multicast note, loan's
  per-transport note, and TSN's VLAN/gate-schedule note.
- Firmware-specific errors (`RCP_FW_ERR_BAD_STATE`/`_VERIFY_FAILED`/
  `_TRANSFER_ERROR`/`_ROLLBACK_FAILED`) are offset to 100+ so they never
  collide with the generic `rcp_errc_t` values (0-8) that session functions
  may also return directly when `rcp_controller_send()` itself fails —
  standing in for cpp-RCP's separate `std::error_code` category, which C
  has no equivalent of.
- **Requirements catalog gap filled**: like authz in v0.19.0,
  `.fusa-reqs.json` had no forward-declared `REQ-FW-*` entries; added
  `REQ-FW-001` through `-008` this milestone.
- Faithfully preserves two API quirks from cpp-RCP rather than "fixing"
  them into something more consistent: `transfer()` and `verify()` both
  accept a `ctx` parameter that is never actually used in the body (each
  builds its own timeout context from `cfg` instead), while `initiate()`,
  `activate()`, and `rollback()` do use the passed `ctx` directly — an
  inconsistency in cpp-RCP's own API, carried over unembellished and noted
  here rather than silently "fixed" beyond what was asked.
- `tests/test_firmware.c` ports all 7 of cpp-RCP's `test_firmware.cpp`
  cases (starts Idle, initiate transitions, double-initiate fails,
  transfer requires Initiated, full happy path, chunking with per-chunk
  progress, rollback resets to Idle).

---
### Phase 7 — Topology & Scalability
---

### 21. Zone Groups (v0.21.0) ✅

`include/rcp/zonegroup.h` + `src/zonegroup.c`: ports cpp-RCP's
`zonegroup.hpp` — `rcp_zonegroup_send()` dispatches one command to every
zone in a `rcp_zone_group_t` concurrently (one background thread per zone)
via any `rcp_registry_t`, collecting per-zone results into a
`rcp_group_response_t`.
- `rcp_zone_group_t` uses a **fixed-capacity array** (`RCP_ZONE_GROUP_MAX =
  8`, generous headroom over the protocol's 5 real zones) rather than
  cpp-RCP's unbounded `std::vector<Zone>` — this is what makes plain struct
  assignment (`b = a;`) a full, independent copy with zero extra code, since
  there's no heap buffer for two structs to alias or for `add()` to
  reallocate out from under a caller still holding the original. An
  unbounded vector would need an explicit deep-copy function to satisfy the
  same "copyable value type" guarantee cpp-RCP's test suite checks for.
- **Scope note**: cpp-RCP's own header comment claims "Priority::Critical
  commands ignore any ErrBusy backpressure," but `GroupRegistry::send_group()`
  contains no such priority-based bypass logic at all — every command is
  sent identically regardless of priority. This port mirrors what's
  actually shipped, the same judgment call made for firmware's SHA-256
  claim, TSN's VLAN tagging, and loan's per-transport note.
- **Requirements catalog gap filled**: like authz and firmware, no
  `REQ-ZG-*` entries existed; added `REQ-ZG-001` through `-006`.
- `tests/test_zonegroup.c` ports all 6 of cpp-RCP's `test_zonegroup.cpp`
  cases, substituting `rcp_mock_registry_new()` (with individual zones
  deregistered as needed) for cpp-RCP's `proxy::ProxyRegistry`, since Zone
  Proxy isn't ported until the next milestone (v0.22.0) and this project's
  process implements milestones strictly in roadmap order. Confirmed via
  20 repeated local runs (debug + 20x ASan/UBSan) with no flakes. Hit and
  fixed the known CFUSA-CY004 false-positive pattern once more — this time
  triggered not by the destination pointer but by `group->len` (a
  legitimate count argument) appearing after `calloc(` on the same line;
  extracted to a local `n` throughout the function, which also simplified
  the rest of the loop bodies.

### 22. Zone Proxy (v0.22.0) ✅

`include/rcp/proxy.h` + `src/proxy.c`: ports cpp-RCP's `proxy.hpp` — two
parts. (1) `rcp_proxy_controller_new()`, a generic decorator (same
vtable-wrapping pattern used since v0.9.0) that enforces a latency budget
at a proxy hop: derives a `now + latency_budget_ms` deadline and forwards
with whichever is tighter, that or the caller's own `ctx` deadline. (2)
`rcp_proxy_registry_new()`, a standalone `rcp_registry_t` implementation
(zone → controller route table) closely mirroring `mock.c`'s own registry
structure and locking discipline (growable entries array, single mutex,
steal-the-array-then-unlock pattern in `close()`), just starting empty
rather than pre-populated with the 5 standard zones.
`rcp_proxy_registry_add_route()` wraps an upstream controller in a proxy
controller and registers it via the generic `rcp_registry_register()`
vtable call — no special-casing needed since registration doesn't care
what kind of controller it's holding.
- `tests/test_proxy.c` ports all 8 of cpp-RCP's `test_proxy.cpp` cases
  (budget-respected send, zero-budget timeout, zone passthrough,
  registry lookup+send, unknown-zone lookup, deregister closes upstream,
  idempotent close, duplicate-route rejection).
- **Requirements catalog gap filled**: added `REQ-PROXY-001` through `-006`
  (none existed previously, following the same pattern as authz, firmware,
  and zone groups).

### 23. Redundancy (v0.23.0) ✅

`include/rcp/redundancy.h` + `src/redundancy.c`: ports cpp-RCP's
`redundancy.hpp` — `rcp_redundancy_controller_new()` holds a primary and a
standby controller for the same zone (same generic-decorator vtable
pattern used since v0.9.0). All sends go to whichever is currently active
(primary by default); on `RCP_ERR_CLOSED` or `RCP_ERR_TIMEOUT` (when
`auto_promote` is enabled) the controller promotes the standby and retries
up to `max_retries` times. `rcp_redundancy_controller_promote()` toggles
the active controller manually — calling it twice returns to primary.
- **Scope note**: cpp-RCP's own header comment claims "`RedundantRegistry`
  wraps two `rcp::Registry` instances (primary/standby)" and that
  "heartbeat health monitoring (via `watchdog::Keeper`) drives promotion,"
  but no `RedundantRegistry` class — nor any watchdog integration — exists
  anywhere in the shipped `redundancy.hpp`; only `RedundantController` is
  implemented. This port mirrors what's actually shipped, the same
  judgment call made for firmware's SHA-256 claim, zone groups' Critical
  bypass claim, TSN's VLAN tagging, and loan's per-transport note.
- `tests/test_redundancy.c` ports all 8 of cpp-RCP's `test_redundancy.cpp`
  cases, including a custom `FailController` test double (ported from
  cpp-RCP's own custom `rcp::Controller` subclass of the same name,
  matching the established custom-vtable test-double pattern already used
  in `test_mdns.c`/`test_powerstate.c`).
- **Requirements catalog gap filled**: added `REQ-RED-001` through `-008`
  (none existed previously, following the established pattern).

### 24. Multi-HPC Federation (v0.24.0) ✅

`include/rcp/federation.h` + `src/federation.c`: ports cpp-RCP's
`federation.hpp` — `rcp_federation_registry_new(local_id)` is a standalone
`rcp_registry_t` implementation whose `lookup()` prefers a locally
registered controller, and falls through to a time-bounded remote-HPC
lease (`rcp_federation_registry_add_lease()`) when no local registration
exists. An expired lease (`rcp_monotonic_ms() >= expires_at_ms`) causes
`lookup()` to return `RCP_ERR_NOT_FOUND` until refreshed.
- **Deviation note**: cpp-RCP's `add_lease(Zone, Lease)` takes a raw
  `Lease` struct whose `remote_ctrl` field is a `std::shared_ptr` the
  caller must populate correctly, sharing ownership implicitly via the
  struct copy. This port instead exposes
  `rcp_federation_registry_add_lease()` taking the remote controller
  directly and retaining it internally — there's no public `Lease` type
  with an ownership-ambiguous pointer field for a caller to get wrong.
- `register_ctrl()`/`deregister()`/`controllers()` only ever touch the
  local-controller table, never leases — matching cpp-RCP's own map
  separation (`local_` vs `leases_`) exactly: leases are visible only
  through `lookup()`'s fallback path.
- `tests/test_federation.c` ports all 8 of cpp-RCP's `test_federation.cpp`
  cases (local-preferred-over-lease, remote-lease-when-no-local,
  expired-lease, revoke-lease, local_id preserved, duplicate-zone
  rejection, close closes both tables, closed-registry lookup).
- **Requirements catalog gap filled**: added `REQ-FED-001` through `-008`
  (none existed previously).

---
### Phase 8 — Tooling
---

### 25. Observability (v0.25.0) ✅

`include/rcp/observe.h` + `src/observe.c`: ports cpp-RCP's `observe.hpp` —
`rcp_observe_controller_new()` wraps any controller (same generic-decorator
vtable pattern used since v0.9.0) and records a latency span plus
`rcp.commands.total`/`rcp.commands.errors` counters around every `send()`,
exported via a caller-supplied `rcp_metrics_sink_t`.
- Since C has no virtual dispatch, `MetricsSink` becomes a `(vtable, ctx)`
  pair (`rcp_metrics_sink_t` + `rcp_metrics_sink_vtable_t`) rather than an
  abstract base class — the same borrowed-callback convention already used
  by `rcp_sim_handler_fn`/`rcp_watchdog_health_fn`. `rcp_noop_metrics_sink()`
  and `rcp_in_memory_sink_*()` (a real, thread-safe, growable span
  collector for test/debug use) port cpp-RCP's `NoopSink`/`InMemorySink`
  directly; cpp-RCP's own `CountingSink` is private to its test file, so it
  stays a private test double here too (`tests/test_observe.c`).
- **Deviation note**: span timestamps use `rcp_monotonic_ms()` (millisecond
  resolution) rather than cpp-RCP's `std::chrono::steady_clock`
  (sub-microsecond resolution) — this project has no higher-resolution
  clock primitive anywhere else, and every other module's timing already
  operates at millisecond granularity, so a new one just for this module
  would be inconsistent scope creep.
- `tests/test_observe.c` ports all 7 of cpp-RCP's `test_observe.cpp` cases.
  Confirmed via 20 repeated local runs (debug + 20x ASan/UBSan) with no
  flakes.
- **Requirements catalog gap filled**: added `REQ-OBS-001` through `-008`
  (none existed previously).

### 26. Admin API (v0.26.0) ✅

`include/rcp/admin.h` + `src/admin.c`: ports cpp-RCP's `admin.hpp` — an
in-process (not HTTP-bound, matching cpp-RCP's own scope: "an actual HTTP
binding is out of scope") admin interface: `rcp_admin_server_zones()` lists
registered controllers, `rcp_admin_server_subscribe()`/`_emit()` provide an
SSE-style event push channel, and `rcp_admin_server_record_counter()`/
`_metrics_text()` render Prometheus text-format counters.
- **Bugs found and fixed during the port, not carried into the C
  translation**: my own first draft of `zones()` and `emit()` each used a
  small hardcoded internal buffer (16 controllers / 64 subscribers) to
  stage results before copying to the caller — silently dropping anything
  beyond that count regardless of the caller's own `cap` argument or
  subscriber count. Both were caught before shipping and rewritten to size
  the internal buffer to the real count first (two-call `size-then-fill`
  pattern for `zones()`, matching how many other registry-style APIs in
  this codebase already work; a lock-protected heap-allocated snapshot for
  `emit()`).
- **Deviation note**: cpp-RCP's `emit()` holds its mutex for the duration
  of every subscriber callback invocation; this port invokes callbacks
  outside the lock instead, matching the established convention from
  `watchdog.c`/`deadline.c`/`powerstate.c` — a subscriber that calls back
  into the same server cannot deadlock. Event timestamps use
  `rcp_monotonic_ms()` rather than cpp-RCP's wall-clock
  `std::chrono::system_clock`, since this project has no wall-clock
  primitive anywhere else.
- `tests/test_admin.c` ports all 6 of cpp-RCP's `test_admin.cpp` cases.
  Confirmed via 20 repeated local runs (debug + 20x ASan/UBSan) with no
  flakes.
- **Requirements catalog gap filled**: added `REQ-ADMIN-001` through `-008`
  (none existed previously).

### 27. Record & Replay (v0.27.0) ✅

`include/rcp/record.h` + `src/record.c`: ports cpp-RCP's `record.hpp` —
`rcp_record_controller_new()` wraps any controller (same generic-decorator
vtable pattern used since v0.9.0) and appends a timestamped entry to a
`rcp_record_t` for every Command/Response pair; `rcp_playback_run_all()`
replays a Record against a target controller using the recorded
inter-entry timing (scaled by `speed_factor`).
- **Deviation note**: entry timestamps use `rcp_monotonic_ms()` rather than
  cpp-RCP's wall-clock `std::chrono::system_clock` — a genuine improvement
  here, not just reduced precision: a monotonic clock can never run
  backward (unlike wall-clock time under NTP adjustment), so it is
  strictly better suited to this module's own "timestamps never decrease"
  guarantee. The on-disk binary log format is this port's own (millisecond
  timestamps, native byte order) rather than a byte-for-byte match of
  cpp-RCP's log format, since the format is a local debug/tooling artifact
  read back only by this same library, not a cross-language wire format.
- **Bug found and fixed, not reproduced**: cpp-RCP's `Playback::run_all()`
  iterates `Record::entries()` (a direct, unsynchronized reference into the
  `Record`'s internal `std::vector`) with no lock, while `Record::append()`
  holds a mutex to push into that same vector — a genuine, if narrow, data
  race between concurrent recording and playback (a vector reallocation
  mid-iteration could invalidate what playback is reading). This port's
  `rcp_playback_run_all()` takes a locked snapshot of the record's entries
  before starting playback, closing that window entirely.
- Hit and fixed a real (non-false-positive) `CFUSA-A007` finding (CERT-C
  ERR33-C, unchecked `fwrite()`/`fclose()` return values) in
  `rcp_record_write_binary()` — every write is now checked, and the first
  failure aborts the write and returns an error rather than silently
  producing a truncated log file.
- `tests/test_record.c` ports all 7 of cpp-RCP's `test_record.cpp` cases
  (captures entries, sequential entries, `write_binary` creates a file,
  monotonically non-decreasing timestamps, forwards the inner result
  unchanged, concurrent appends, playback against a target). Confirmed via
  20 repeated local runs (debug + 20x ASan/UBSan) with no flakes.
- **Requirements catalog gap filled**: added `REQ-REC-001` through `-008`
  (none existed previously).

### 28. Config (v0.28.0) ✅

`include/rcp/config.h` + `src/config.c`: ports cpp-RCP's `config.hpp` — a
hand-rolled, minimal JSON manifest parser (not a general-purpose JSON
library, matching cpp-RCP's own documented scope) that populates a
`rcp_registry_t` with mock controllers per zone entry.
`rcp_config_parse_json()` and `rcp_config_load()` port the same
substring-scanning algorithm as cpp-RCP's `parse_json()`/`load()`
(unbounded quote-search per field, tolerant of surrounding whitespace/
formatting, understanding only this module's own manifest schema).
- **Deviation note**: cpp-RCP signals malformed input by throwing
  `config::ParseError` (a `std::runtime_error` subclass). C has no
  exceptions, so `rcp_config_parse_json()`/`_load()` return
  `RCP_CFG_ERR_PARSE` (offset to 100+, matching firmware.h's convention)
  and write a description into a caller-supplied buffer instead.
- **Scope note**: this project's own `ROADMAP.md` entry for this milestone
  (written up-front from cpp-RCP's roadmap prose, before this port existed)
  said "YAML/JSON... with hot-reload" — but cpp-RCP's own shipped
  `config.hpp` implements neither: YAML support is explicitly documented
  as requiring an external, unshipped YAML→JSON shim, and there is no
  reload/watch mechanism anywhere in the module (`load()` is a one-shot
  parse-then-register call). This port mirrors what's actually shipped,
  the same judgment call made for every other roadmap-over-promises note
  in this project.
- `tests/test_config.c` ports all 5 of cpp-RCP's `test_config.cpp` cases,
  substituting `rcp_proxy_registry_new()` (which starts empty) for
  cpp-RCP's `proxy::ProxyRegistry` in the same role.
- **Requirements catalog gap filled**: added `REQ-CFG-001` through `-006`
  (none existed previously).

### 29. Code Generation (v0.29.0) ✅

`tooling/zone_manifest_schema.json`: a JSON Schema document for the zone
manifest format `rcp/config.h` (v0.28.0) already loads.
- **Scope note — the largest gap yet between roadmap prose and shipped
  code**: this project's own `ROADMAP.md` entry (written from cpp-RCP's
  roadmap prose before this milestone was reached) promised "a generator
  stub emitting `fusa:req`-annotated C stubs." cpp-RCP's own `ROADMAP.md`
  goes further still, describing an `rcptool gen <manifest.yaml>` CLI that
  "generates typed C++ controller stubs with `// fusa:req` annotations
  pre-populated," complete with matching test skeletons and requirements
  entries. **None of that exists anywhere in cpp-RCP's actual repository**
  — no `rcptool`, no generator source, no generated-stub examples, not
  even a stub interface header. Searching the entire tree turns up exactly
  one artifact for this whole milestone: `tooling/zone_manifest_schema.json`,
  a JSON Schema describing the manifest format that `config.hpp`'s
  hand-rolled parser already validates informally through its own parsing
  logic. This port ships the direct equivalent — nothing more, since
  nothing more was ever built. (cpp-RCP's own `.fusa-reqs.json` has no
  `REQ-CODEGEN-*` entries either, confirming this was never treated as
  testable, shippable code even upstream.)
- The `transport` enum is scoped to what this project has actually shipped
  as of v0.29.0 (`mock`, `udp`, `shmem`, `tls`, `tsn`) rather than
  cpp-RCP's own schema file, which lists every protocol bridge across its
  *entire* finished history (gRPC, REST, SOME/IP, CAN, DDS, MQTT, LIN, UDS,
  DoIP) — those don't exist in cpp-RCP's own repository until milestones
  31–38, well after this one, so listing them here would describe
  capabilities this project hasn't built yet. Both `priority` and
  `transport` are documented as currently informational-only, since
  `rcp_config_load()` (like cpp-RCP's own `load()`) ignores both fields
  and always registers a plain mock controller regardless of what the
  manifest requests.
- No new `.c`/`.h`/test files, and no new `REQ-*` catalog entries (there is
  no executable requirement to trace for a schema document).

### 30. Dynamic Data (v0.30.0) ✅

`include/rcp/dyndata.h` + `src/dyndata.c`: ports cpp-RCP's `dyndata.hpp` —
`rcp_schema_registry_t` maps a `rcp_schema_id_t` to a human-readable name
and optional field descriptors; `rcp_dynamic_payload_t` is a self-describing
envelope (4-byte big-endian schema ID + raw data blob) with
`rcp_dynamic_payload_encode()`/`_decode()`.
- Both `rcp_schema_registry_add()` and `_lookup()` deep-copy the
  variable-length `fields` array (via `malloc`+`memcpy`, freed with
  `rcp_schema_entry_free()`), matching cpp-RCP's own by-value
  `std::vector<FieldDescriptor>` copy semantics — the registry never
  aliases a caller's array, and a looked-up entry is fully independent of
  the registry's internal storage.
- `rcp_dynamic_payload_decode()` on a buffer shorter than 4 bytes returns a
  zeroed payload (`schema_id = 0`, empty data) rather than erroring —
  deliberately mirroring cpp-RCP's own `decode()`, whose test suite
  explicitly verifies this lenient behavior (not a bug to "fix").
- `tests/test_dyndata.c` ports all 7 of cpp-RCP's `test_dyndata.cpp` cases.
  Confirmed via 20 repeated local runs (debug + 20x ASan/UBSan) with no
  flakes. Hit and fixed the known CFUSA-CY004 false-positive pattern once
  more (`malloc(4 + dp->data.len)`); re-grepped the whole codebase
  afterward, no other occurrences.
- **Requirements catalog gap filled**: added `REQ-DYN-001` through `-006`
  (none existed previously).

---
### Phase 9 — Remote Access
---

### 31. gRPC Bridge (v0.31.0) ✅

`include/rcp/grpcbridge.h` + `src/grpcbridge.c`: ports cpp-RCP's
`grpcbridge.hpp` — a compile-time interface stub matching the same pattern
already established for `tls.h` at v0.7.0. No generated gRPC stub (from an
`rcp.proto`) is linked, so `send()`/`subscribe()` always return
`RCP_ERR_NOT_SUPPORTED`; `zone()` returns the configured zone regardless,
and `close()` always succeeds. Considerably simpler than the TLS stub
(no `ZoneServer`/registry-dial surface), matching cpp-RCP's own
`grpcbridge.hpp`, which likewise only stubs a bare `Controller`.
`tests/test_grpcbridge.c` ports all 4 of cpp-RCP's `test_grpcbridge.cpp`
cases.
- **Requirements catalog gap filled**: added `REQ-GRPC-001` through `-004`
  (none existed previously).

### 32. REST Bridge (v0.32.0) ✅

`include/rcp/restbridge.h` + `src/restbridge.c`: ports cpp-RCP's
`restbridge.hpp` — structurally identical to the already-shipped
`grpcbridge.h`/`grpcbridge.c` (v0.31.0): a compile-time interface stub with
no HTTP client backend linked, so `send()`/`subscribe()` always return
`RCP_ERR_NOT_SUPPORTED`, `zone()` returns the configured zone regardless,
and `close()` always succeeds. `rcp_rest_config_t` renames cpp-RCP's
`base_url`/`request_timeout` fields verbatim (`request_timeout_ms` as a
`uint64_t` millisecond count, matching the `rpc_timeout_ms` convention
already used for `rcp_grpc_config_t`).
- **Scope note**: despite the file-header comment in cpp-RCP's own
  `restbridge.hpp` mentioning "REST/HTTP protocol bridge" and this
  project's earlier roadmap prose describing "HTTP/SSE bridge interface
  stub," cpp-RCP's actual shipped `RestController` has no SSE
  (server-sent-events) surface at all — `subscribe()` is just as
  unconditionally stubbed as `send()`. This port mirrors what's actually
  there, not the aspirational comment, consistent with the same judgment
  call made for milestones 26–29.
- `tests/test_restbridge.c` ports all 4 of cpp-RCP's `test_restbridge.cpp`
  cases, mirroring `test_grpcbridge.c` exactly.
- **Requirements catalog gap filled**: added `REQ-REST-001` through `-004`
  (none existed previously).

---
### Phase 10 — Automotive Protocol Bridges
---

### 33. SOME/IP Bridge (v0.33.0) ✅

`include/rcp/someipbr.h` + `src/someipbr.c`: ports cpp-RCP's `someipbr.hpp`
— the same compile-time stub pattern as `restbridge`/`grpcbridge`, this
time addressed by `service_id`/`instance_id`/`method_id` (`uint16_t`, per
SOME/IP's own wire addressing scheme) plus a `timeout_ms` rather than a
URL/hostname. `send()`/`subscribe()` always return
`RCP_ERR_NOT_SUPPORTED` (no vsomeip or equivalent backend linked);
`zone()` returns the configured zone; `close()` always succeeds.
- `tests/test_someipbr.c` ports all 4 of cpp-RCP's `test_someipbr.cpp`
  cases, mirroring `test_restbridge.c`/`test_grpcbridge.c`.
- **Requirements catalog gap filled**: added `REQ-SOMEIP-001` through
  `-004` (none existed previously).

### 34. CAN Bridge (v0.34.0) ✅

`include/rcp/canbr.h` + `src/canbr.c`: ports cpp-RCP's `canbr.hpp` — the
same compile-time stub pattern as the other protocol bridges, configured
by `can_id_base` (base arbitration ID), `fd_mode` (CAN-FD frames), and
`timeout_ms` rather than a URL/hostname/service triad. `send()`/
`subscribe()` always return `RCP_ERR_NOT_SUPPORTED` (no SocketCAN or
hardware CAN driver backend linked); `zone()` returns the configured
zone; `close()` always succeeds.
- `tests/test_canbr.c` ports all 4 of cpp-RCP's `test_canbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-CAN-001` through `-004`
  (none existed previously).

### 35. DDS Bridge (v0.35.0) ✅

`include/rcp/ddsbr.h` + `src/ddsbr.c`: ports cpp-RCP's `ddsbr.hpp` — the
same compile-time stub pattern as the other protocol bridges, configured
by `topic_prefix` (DDS topic names: `{prefix}/command`,
`{prefix}/response`, default `"rcp"`), `domain_id`, and `timeout_ms`.
`send()`/`subscribe()` always return `RCP_ERR_NOT_SUPPORTED` (no OMG DDS
implementation such as FastDDS or Cyclone DDS linked); `zone()` returns
the configured zone; `close()` always succeeds.
- `tests/test_ddsbr.c` ports all 4 of cpp-RCP's `test_ddsbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-DDS-001` through `-004`
  (none existed previously).

### 36. MQTT Bridge (v0.36.0) ✅

`include/rcp/mqttbr.h` + `src/mqttbr.c`: ports cpp-RCP's `mqttbr.hpp` —
the same compile-time stub pattern as the other protocol bridges,
configured by `broker_url` (default `"tcp://localhost:1883"`),
`topic_prefix` (default `"rcp"`), `qos`, and `timeout_ms`. `send()`/
`subscribe()` always return `RCP_ERR_NOT_SUPPORTED` (no MQTT client
library such as Eclipse Paho linked); `zone()` returns the configured
zone; `close()` always succeeds.
- `tests/test_mqttbr.c` ports all 4 of cpp-RCP's `test_mqttbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-MQTT-001` through `-004`
  (none existed previously).

### 37. LIN Bridge (v0.37.0) ✅

`include/rcp/linbr.h` + `src/linbr.c`: ports cpp-RCP's `linbr.hpp` — the
simplest protocol-bridge config yet, just `frame_id` (`uint8_t`, default
`0x10`) and `timeout_ms`. `send()`/`subscribe()` always return
`RCP_ERR_NOT_SUPPORTED` (no SocketCAN LIN driver or dedicated LIN
hardware API backend linked); `zone()` returns the configured zone;
`close()` always succeeds.
- `tests/test_linbr.c` ports all 4 of cpp-RCP's `test_linbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-LIN-001` through `-004`
  (none existed previously).

### 38. UDS Bridge (v0.38.0) ✅

`include/rcp/udsbr.h` + `src/udsbr.c`: ports cpp-RCP's `udsbr.hpp` — the
same compile-time stub pattern as the other protocol bridges, configured
by `routine_id` (default `0x0100`), `p2_timeout_ms` (default P2 server
timeout, default 50), and `p2ext_timeout_ms` (extended P2* timeout,
default 5000), matching ISO 14229's own RoutineControl (SID 0x31) timing
parameters. `send()`/`subscribe()` always return `RCP_ERR_NOT_SUPPORTED`
(no UDS stack integration linked); `zone()` returns the configured zone;
`close()` always succeeds.
- `tests/test_udsbr.c` ports all 4 of cpp-RCP's `test_udsbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-UDS-001` through `-004`
  (none existed previously).

### 39. DoIP Bridge (v0.39.0) ✅

`include/rcp/doipbr.h` + `src/doipbr.c`: ports cpp-RCP's `doipbr.hpp` —
the last of the protocol-bridge stubs, configured by `server_ip` (no
default — `NULL` until the caller sets it, matching cpp-RCP's own
must-be-set-by-caller `std::string`), `server_port` (default `13400`),
`logical_addr` (default `0x0001`), and `tcp_timeout_ms` (default 2000).
`send()`/`subscribe()` always return `RCP_ERR_NOT_SUPPORTED` (no DoIP
stack integration linked); `zone()` returns the configured zone;
`close()` always succeeds.
- `tests/test_doipbr.c` ports all 4 of cpp-RCP's `test_doipbr.cpp` cases.
- **Requirements catalog gap filled**: added `REQ-DOIP-001` through `-004`
  (none existed previously).
- **Phase note**: this completes Phase 10 (Automotive Protocol Bridges,
  milestones 33–39). All seven bridges (SOME/IP, CAN, DDS, MQTT, LIN,
  UDS, DoIP) ship the identical compile-time-stub pattern established at
  v0.31.0 (gRPC) — none of cpp-RCP's own bridge modules link a real
  backend, so none of the C ports do either; each is a faithful,
  requirement-traced placeholder ready for a concrete backend to be
  wired in later.

---
### Phase 11 — Platform
---

### 40. RTOS / Bare-Metal (v0.40.0) ✅

`PORTABILITY.md`: the portability audit flagged in this project's
original scope-adaptation plan. cpp-RCP's own v0.40.0 shipped a new
`capi.h`/`capi_impl.hpp` pure-C API layer so RTOS targets that can't
link a C++17 runtime could still call in; c-RCP's public API already
*is* that layer, so a second C API on top would be pure duplication.
Instead, the audit identifies: (1) the wire/protocol/codec logic
(`rcp.c`, `wire.c`, `e2e.c`, `faultinject.c`, `dyndata.c`) as already
100% RTOS-portable with zero OS dependency; (2) every other module as
depending on exactly one seam — `src/platform.h`/`platform.c`'s
mutex/cond/thread/clock primitives — meaning a Zephyr or FreeRTOS
backend is an additive third `#if defined(...)` branch in one file, not
a redesign; (3) two concrete gaps a real RTOS port would still need to
close (static/pool allocation in place of `malloc`/`calloc` at ~117 call
sites; collapsing the one-thread-per-decorator pattern used by
`watchdog.c`/`deadline.c`/`powerstate.c`/`sim.c`/`prioqueue.c`/
`zonegroup.c` into fewer cooperative tasks for fixed-task-count
hardware) — both explicitly deferred as separate future work rather
than silently declared "done."
- **No new `.c`/`.h`/test files, and no new `REQ-*` catalog entries**:
  consistent with the Code Generation milestone (v0.29.0), an audit
  document is not executable code and has no requirement to trace.

---
### Phase 12 — Certification & Formal Methods
---

### 41. Formal Verification (v0.41.0) ✅

`tla/HealthStateMachine.tla`, `tla/WatchdogProtocol.tla`,
`tla/AntiReplayGuard.tla`, `FORMAL_VERIFICATION.md`: ports cpp-RCP's
three TLA+ specs verbatim (byte-identical — confirmed via `diff`). These
specs describe state transitions and safety properties (SP1: no direct
`Healthy`→`Faulted` transition; SP2: no double-acceptance of a replayed
sequence number) at a level with no C++ or C syntax anywhere in the
model, and c-RCP's `watchdog.c`/`e2e.c` implement the identical state
machine and sliding-window algorithm as cpp-RCP's `watchdog.hpp`/
`e2e.hpp` — same states, same transition guards, same 32-slot window —
so the specs apply unmodified. Only `FORMAL_VERIFICATION.md`'s "Mapping
to Implementation" table changes, pointing at this project's actual C
symbol names (`zone_state_t.health`/`.misses` in `src/watchdog.c`,
`rcp_e2e_replay_guard_t.bitmap[]`/`.high_water` in `src/e2e.c`) instead
of cpp-RCP's C++ ones, plus a note that `HealthStateMachine.tla`'s
single-`MaxMiss` model is a slight abstraction of c-RCP's actual
two-threshold `degrade_after`/`fault_after` design — a strict
refinement that preserves the same `NoDirectFault` safety property.
- **No new `REQ-*` catalog entries**: the specs verify safety properties
  of watchdog/E2E behavior already covered by `REQ-WDG-*`/`REQ-E2E-*`
  from earlier milestones, not new requirements.

### 42. ISO 21434 / Cybersecurity (v0.42.0) ✅

`CYBERSECURITY.md`: documents the five-layer defense already implemented
across earlier milestones — TLS (v0.7.0), authorization (v0.19.0), E2E
anti-replay (v0.13.0, formally verified at v0.41.0), rate limiting
(v0.22.0), and firmware integrity (v0.20.0) — plus the threat-to-
countermeasure mapping and IEC 62443 SL-2 FR1–FR7 status table. Ports
cpp-RCP's `CYBERSECURITY.md` structure, updated to reference c-RCP's own
C symbol names in place of cpp-RCP's C++ class names.
- **`.github/workflows/release.yml` gains a new step**: `cfusa iec62443
  --sl SL-2 --output iec62443-gap-report.json`, wired in identically to
  the existing `iso26262`/`iec61508`/`do178` gap-report steps (same
  `|| true` non-blocking pattern — the workflow's job is to generate the
  artifact, not gate on it). This closes the actual gap in this
  project's own tooling: `.fusa-iec62443.json` (target `SL-2`) had
  existed since the v0.1.0 scaffold, but nothing ever invoked `cfusa
  iec62443` against it until now, so no gap report had ever been
  produced for c-RCP specifically.
- TARA already exists as an auto-generated artifact (`tara.md`/
  `tara.json`, regenerated every tagged release by `cfusa tara` since
  the initial scaffold) — `CYBERSECURITY.md` references it rather than
  duplicating it.
- **No new `.c`/`.h`/test files or `REQ-*` catalog entries**: this
  milestone documents and gap-analyzes existing security controls
  rather than adding new ones.

### 43. Certification (v0.43.0) ✅

`AUDIT_PACK.md`: the final certification-evidence document, mirroring
cpp-RCP's own `AUDIT_PACK.md` structure and ASIL-D derogation rationale
(the underlying architecture — single-channel zonal network with E2E
protection — is identical between both projects), but reporting c-RCP's
own measured numbers rather than copying cpp-RCP's:
- **314 requirements across 44 groups** (vs. cpp-RCP's 198 across 24 —
  this project's catalog grew substantially larger across the 8
  protocol-bridge milestones and platform/verification docs cpp-RCP's
  own catalog doesn't separately enumerate).
- **Real, measured coverage** from this project's own
  `coverage-report.json`: 83.33% line (3489/4187), 86.92% function,
  meeting the ≥80% DAL-B line-coverage threshold. Branch/MC/DC coverage
  is flagged as an **honest open item** rather than invented — this
  project's `lcov --capture` step in `release.yml` doesn't currently
  pass a branch-coverage flag, so `cfusa coverage` reports `0/0` for
  branch data; closing that gap is documented as follow-up work rather
  than papered over with a fabricated percentage (unlike cpp-RCP's own
  `AUDIT_PACK.md`, whose per-module coverage table has no corresponding
  machine-generated source `cfusa coverage` produces, and so isn't
  reproduced here).
- A full CI-gate summary table (17 gates spanning static analysis
  through the audit pack itself) and the change-impact procedure,
  ported near-verbatim since both projects run the identical `cfusa`/
  `cpfusa` gate set.
- **No new `.c`/`.h`/test files or `REQ-*` catalog entries**: like
  milestones 40 and 42, this is a certification-evidence document, not
  new executable code.

This completes all 43 milestones of the original roadmap.

---
### Post-roadmap: RELAY conformance & security audit findings
---

A RELAY-ecosystem conformance audit (2026-07-27) filed five issues
against this repository (`SoundMatt/c-RCP#8`–`#12`). Each is being
addressed as its own versioned release, in dependency order, following
the same PR/CI-green/tag discipline as every milestone above.

### 44. Wire decoder integer-overflow fix (v0.44.0) ✅ — closes #9

`src/wire.c`'s `rcp_wire_decode_command()`/`_response()`/`_status()`
validated an attacker-controlled `uint32_t body_len` against the actual
buffer length as `if (len < RCP_WIRE_HEADER_LEN + body_len)`. On any
32-bit-`size_t` target (the bare-metal/RTOS profile this library is
built for, per `PORTABILITY.md`), that addition can wrap around for
`body_len` values near `UINT32_MAX`, defeating the short-frame guard
entirely and reaching `rcp_bytes_dup()` with an attacker-controlled
length far exceeding the real buffer — a heap over-read on untrusted
UDP input. `RCP_WIRE_MAX_PAYLOAD` (`include/rcp/wire.h`) already existed
as a ceiling constant but was never used as a bound check.
- **Fix**: all three decoders now reject `body_len > RCP_WIRE_MAX_PAYLOAD`
  *before* computing the addition, closing the overflow independent of
  `size_t` width.
- **Regression test verified genuine, not just added**: `tests/test_wire.c`
  gained 4 new cases. Confirmed by temporarily reverting the fix locally
  that `test_decode_command_rejects_body_len_just_over_max_even_with_real_buffer`
  (which allocates a real, fully-sized buffer so the *old* length check
  alone would have let the frame through) fails without the new ceiling
  check and passes with it — the other 3 new cases (header-only buffers)
  pass either way on this 64-bit CI host, since the pre-fix check already
  happened to reject a claimed-length-vastly-exceeding-actual-buffer case
  for unrelated reasons on 64-bit `size_t`; only the real-buffer case
  proves the fix's actual guarantee.
- **Requirements catalog**: added `REQ-UDP-013`.

### 45. `rcp_zone_string()` PascalCase + `rcp_zone_from_string()` (v0.45.0) ✅ — closes #11

`src/rcp.c`'s `rcp_zone_string()` returned lowercase kebab-case
(`"front-left"` etc.), but RELAY spec §10.4/§15.7.5 mandate PascalCase
(`"FrontLeft"`) as the canonical zone-name string form used in routing
and `Status.ToMessage()`.
- **Deviation from cpp-RCP's own precedent, deliberately**: cpp-RCP
  fixed the identical upstream defect (also present in go-RCP, tracked
  there as issue #51) differently — it left its own `to_string(Zone)`
  untouched in kebab-case and added a *separate*, RELAY-only pair
  (`zone_to_relay_id`/`zone_from_relay_id`) inside `adapt.hpp`. This
  fix instead follows go-RCP's own resolution and this issue's own
  literal suggested-fix text: `rcp_zone_string()` itself now returns
  PascalCase directly, with a new `rcp_zone_from_string()` accepting
  both the new PascalCase form and the legacy kebab-case form (for
  backward compatibility with anything that persisted or logged the
  old strings) — mirroring go-RCP's dual-accept `ZoneFromString`
  exactly. This also means milestone 46's `adapt.c` can call
  `rcp_zone_string()`/`rcp_zone_from_string()` directly rather than
  needing a second, RELAY-only pair of functions.
- Two real call sites (`src/mdns.c`'s instance-name builder,
  `tests/test_mdns.c`'s expectation) both call `rcp_zone_string()`
  dynamically rather than hardcoding the old casing, so both adapted
  with no further changes needed — confirmed by re-reading both files
  before touching anything.
- **Requirements catalog**: added `REQ-ZONE-009` (`REQ-ZONE-002` was
  already taken by an unrelated pre-existing requirement — caught via
  a duplicate-ID check on `.fusa-reqs.json` before shipping, not after).
  Updated `REQ-ZONE-001`'s text to describe PascalCase instead of
  generic "human-readable."
- 4 new test cases in `tests/test_rcp.c`: exact PascalCase strings for
  all 5 zones, a round-trip through both new functions, legacy
  kebab-case acceptance, and unknown/NULL rejection.

### 46. `Adapt()`/`ToMessage()`/`FromMessage()`/`SpecVersion` (v0.46.0) ✅ — closes #10

The largest of the five conformance-audit fixes. c-RCP had none of
RELAY's mandatory §17 requirements 6 (`Adapt()`), 9 (`ToMessage()`/
`FromMessage()`), or 12 (`SpecVersion`) — `include/relay/relay.h` only
bundled the error sentinels and `Context`, explicitly noting "not a
full RELAY binding." This milestone fleshes it out and ports cpp-RCP's
`adapt.hpp` pattern to C99.
- **`include/relay/relay.h` grows substantially**: `RELAY_SPEC_VERSION`
  (`"1.11"` — deliberately the *current* spec version, not cpp-RCP's own
  stale `"1.10"`, since conforming to the version that motivated this
  whole remediation effort would be self-defeating), `relay_protocol_t`
  + `relay_protocol_string()`, `relay_message_t` (the universal envelope
  — id/payload/meta all owned, with `relay_message_init/free/set_id/
  set_meta/get_meta`), `relay_backpressure_t` +
  `relay_subscriber_options_t`, `relay_message_channel_t` (a concrete
  bounded queue mirroring `rcp_status_channel_t`'s exact
  mutex/condvar/circular-buffer pattern, since C has no templates for a
  generic `Channel<T>`), and `rcp_relay_caller_t` (a vtable-based
  Node+Caller — C has no interface inheritance, so one vtable covers
  both roles).
  - Uses its own `relay_bytes_t` rather than `rcp.h`'s `rcp_bytes_t`:
    `relay.h` must stay protocol-agnostic (the envelope is shared by
    every adapter — CAN, DDS, LIN, MQTT, RCP, SOMEIP), and `rcp_bytes_t`
    is only defined later in `rcp.h`, which itself includes this header
    first — a real ordering constraint caught while writing the code,
    not a stylistic choice.
- **`include/rcp/adapt.h` + `src/adapt.c`**: `rcp_status_to_message()`/
  `rcp_response_to_message()`/`rcp_message_to_command()` implement the
  exact §15.7.5 field mapping cpp-RCP's `adapt.hpp` already
  established; `rcp_adapt()` wraps an `rcp_controller_t` (retaining it)
  as a vtable-based caller. `subscribe()` spawns one detached thread per
  subscription (§10.5) that drains the wrapped controller's
  `rcp_status_channel_t` and pushes mapped messages to the returned
  `relay_message_channel_t`, applying the requested back-pressure
  policy (DropNewest/DropOldest/Block), and exits — closing the output
  channel — when the underlying status channel closes.
- **`RCP_SPEC_VERSION`** (`rcp.h`) is a distinct macro aliasing
  `RELAY_SPEC_VERSION`, mirroring go-RCP's two-symbol pattern
  (`rcp.SpecVersion` aliasing `relay.SpecVersion`) rather than cpp-RCP's
  single-symbol approach, so callers get an RCP-specific answer without
  reaching into `relay.h` directly.
- **`src/clock.c`** gains `rcp_wallclock_ms()` (wall-clock epoch
  milliseconds) — needed for `Message.timestamp` and not previously
  exposed; `clock.h` only had `rcp_monotonic_ms()`.
- **Golden-vector conformance test**: `tests/test_adapt.c` pins
  `rcp_status_to_message()` against the same
  `RELAY spec/vectors/rcp-status.json` (v0.3) golden vector cpp-RCP's
  own `test_relay.cpp` pins against — confirmed by reading that
  reference file directly rather than assuming the mapping. 20 test
  cases total, matching (and in the channel/lifecycle cases, slightly
  exceeding) cpp-RCP's own `test_relay.cpp` rigor — including running
  the multi-threaded subscribe test 10 times under ASan/UBSan to catch
  any intermittent race, with zero failures.
- **Requirements catalog**: added `REQ-RELAY-001` through `-013`.

### 47. CLI binary — `version`/`capabilities`/`status` (v0.47.0) ✅ — closes #8

The P0 finding: c-RCP shipped as a library only, with no `add_executable`
of any kind — RELAY spec §17 requirement 7 (v1.11, which removed the
"C++ (or other) library" CLI waiver) requires every implementation to
expose `version`/`capabilities`/`status` as a runnable CLI, gated behind
`-DRELAY_BUILD_CLI=ON` when there's no default binary.
- **`include/rcp/version.h`** (`RCP_VERSION`), **`include/rcp/cli.h`** +
  **`src/cli.c`** (`rcp_cli_run()`, always compiled into the `rcp`
  library so it's unit-testable without a subprocess — mirrors
  cpp-RCP's own header-only `cli.hpp` design goal), and **`cli/main.c`**
  (a thin wrapper), ported from cpp-RCP's `cli.hpp`/`main.cpp`.
- **`CMakeLists.txt`** gains `option(RELAY_BUILD_CLI ... OFF)` +
  `add_executable(c-rcp cli/main.c)` gated behind it — the option gates
  only the standalone binary target; `cli.c`'s logic is always
  compiled and testable regardless.
- **Scope deliberately narrower than cpp-RCP's own `cli.hpp`**: cpp-RCP
  additionally implements an optional `send --format json` streaming
  command (a full hand-rolled JSON parser + base64 decoder, for the
  §11.2 crossbar-spoke use case) that is not part of RELAY's mandatory
  conformance surface — only version/capabilities/status are required.
  This port implements just those three plus `--help`, matching the
  actual P0 defect rather than importing ~150 lines of unrequested
  JSON-parsing machinery. `capabilities`'s `"commands"` list honestly
  omits `"send"` rather than overclaiming.
- **Real upstream spec gap found and worked around, not hidden**: the
  RELAY `spec/schemas/cli-version.json` `language` field is a strict
  enum (`"go"|"cpp"|"rust"`, `additionalProperties: false`) with no
  value for a pure-C implementation — confirmed by reading the schema
  directly, and confirmed (by reading `cmd/relay/conform.go`) that
  schema violations are hard `FAIL`s in `relay conform --strict`, not
  warnings, so a literally-honest `"language":"c"` would make this CLI
  fail its own new conformance gate. Uses `"cpp"` pragmatically
  (matching the parent project this port mirrors everywhere else) and
  documents the compromise explicitly in `src/cli.c` rather than
  silently picking a value.
- **Verified against the real `relay conform` tool locally, twice**
  (once for a plain Debug build, once for the ASan/UBSan build) —
  built `SoundMatt/RELAY`'s own `cmd/relay` from source and ran
  `relay conform --strict` against the actual `c-rcp` binary before
  ever wiring this into CI (that's milestone 48): all three documents
  (`§12.1`/`§12.2`/`§12.3`) PASS.
- **Caught a Metric 2 (function annotation density) regression before
  shipping**: `cli/main.c`'s `main()` had no `//cfusa:req` tag, dropping
  the hard 100% gate to 99% — fixed by adding `REQ-CLI-006` and tagging
  it, not by disabling or loosening the gate.
- **Requirements catalog**: added `REQ-CLI-001` through `-006`.

### 48. `relay conform` CI gate (v0.48.0) ✅ — closes #12

The final finding of the five-issue RELAY-ecosystem audit, and a
direct consequence of milestone 47 landing: `.github/workflows/ci.yml`
gains a new `relay-conform` job — checkout `SoundMatt/RELAY`, build its
`cmd/relay` conformance tool from source, build `c-rcp` with
`-DRELAY_BUILD_CLI=ON`, and run `relay conform --strict` against it —
mirroring cpp-RCP's own `ci.yml` job template exactly (`cpp-rcp`
renamed to `c-rcp`, no other structural changes). The main
`build-and-test` matrix's `Configure` step also gains
`-DRELAY_BUILD_CLI=ON`, so the CLI compiles and its unit tests
(`tests/test_cli.c`) run across all 4 platforms in the existing matrix,
not just the dedicated conformance job's single Ubuntu runner.
- **Verified locally before ever touching CI**: built `relay` from
  `SoundMatt/RELAY` source and ran `relay conform --strict` against a
  local `-DRELAY_BUILD_CLI=ON` build — PASS on all three documents —
  the same verification already done once at milestone 47, repeated
  here against the final CI-wired configuration to catch any
  regression before pushing.
- No new `.c`/`.h`/test files: this is CI/workflow wiring only, per the
  issue's own scope note ("filing separately only in case the CLI gets
  added without someone remembering to also add the CI gate").
- **This completes the five-issue RELAY-ecosystem conformance audit**
  (`SoundMatt/c-RCP#8`–`#12`) filed 2026-07-27, each shipped as its own
  versioned release (v0.44.0–v0.48.0) in dependency order, verified
  against the real spec/tooling (not assumed), and each documented
  transparently — including the two places (the zone-string fix, the
  CLI `language` field) where this project's own established precedent
  (mirror cpp-RCP) was deliberately overridden by a more specific,
  better-reasoned choice, spelled out explicitly rather than silently
  diverging.

---
### 49. CI hardening: remove vestigial CFUSA-L004 escape hatch (v0.49.0)
---

A deep compliance audit (2026-07-27) verifying that CI's `cfusa-*` gates
are real (not decorative) surfaced a correction to this project's own
prior understanding: `cfusa-check`/`cfusa-lint` had carried
`continue-on-error: true` since v0.1.0, shielding against a confirmed
c-FuSa tool bug (`CFUSA-L004`, a naive recursion-check false positive,
tracked as `SoundMatt/c-FuSa#59`). Auditing against the *actual* CI-built
c-FuSa binary (not a stale local rebuild — an easy trap, since a local
binary built earlier in this project's history no longer matches what
`git clone --depth=1` fetches fresh on every CI run) showed the bug was
fixed upstream in c-FuSa v0.5.39, and the real, current CI has been
reporting 0 `CFUSA-L004` findings — and 0 errors overall — on every run
since, without anyone noticing the escape hatch was no longer needed.
- Removed `continue-on-error: true` from both `cfusa-check` and
  `cfusa-lint` in `.github/workflows/ci.yml`, restoring them as genuine
  hard gates.
- Cleared the now-stale `DISP-0001` entry from `.fusa-dispositions.json`
  (an "accept" disposition for a finding that no longer occurs is
  inaccurate bookkeeping, not a harmless leftover).
- **Verified against a freshly-rebuilt local c-FuSa binary** (v0.5.44,
  matching CI) before shipping: `check`/`lint`/`analyze`/`cyber`/
  `trace`/`qualify`/`vuln` all genuinely exit 0, not just "look green"
  from a stale cache.
- No new `.c`/`.h`/test files — workflow and disposition-bookkeeping
  changes only.

---
### 50. Coverage maximization, batch 1: branch instrumentation + strerror() functions (v0.50.0)
---

A follow-up audit ("are all requirements and tests maximised with
regard to coverage?") pulled the real `coverage.info` artifact from the
last CI run rather than approximating locally, and found: 83.5% line /
87.7% function coverage, and **67 functions across 23 of 45 source
files never called by any test** — including, surprisingly,
`rcp_strerror()` and `relay_strerror()` (confirmed via grep: zero
references anywhere outside their own definitions, despite being basic
public API). This is the first of several batches closing that gap.

- **Branch-coverage instrumentation enabled**: `lcov --capture`/
  `--remove` in both `ci.yml` and `release.yml` gain
  `--rc branch_coverage=1`. Previously reported a vacuous `0/0`
  ("PASS" by default) in `cfusa coverage`; verified locally (via a
  local coverage build with a matching `gcov` tool) that the flag
  produces real, non-trivial branch data (52.8% on an unfiltered local
  run) rather than assuming the flag name was correct.
- **7 new tests for the "surprisingly untested" string-lookup
  functions**: `rcp_strerror()`/`relay_strerror()` (`test_rcp.c`),
  `rcp_e2e_strerror()` (`test_e2e.c`), `rcp_fw_strerror()`
  (`test_firmware.c`), `rcp_wire_strerror()` (`test_wire.c`),
  `rcp_health_state_string()` (`test_watchdog.c`),
  `rcp_power_state_string()` (`test_powerstate.c`) — each asserting a
  unique, non-empty string per enum value, matching the established
  pattern already used for `rcp_zone_string()`/
  `rcp_response_status_string()`.
- **Requirements catalog gap**: none of these 7 functions had a
  `.fusa-reqs.json` entry at all (part of why they went untested —
  there was no requirement for a test to trace to). Added
  `REQ-ERR-012`, `REQ-RELAY-014`, `REQ-E2E-009`, `REQ-FW-009`,
  `REQ-UDP-014`, `REQ-WDG-009`, `REQ-PWR-009`.
- **Verified the coverage improvement is real**, not just "tests
  added": pulled the raw `.info` record for each of the 7 target
  functions post-change and confirmed each now shows a non-zero hit
  count (6–45 hits depending on how many test cases exercise it),
  after first catching and fixing a mismatched lcov record-format
  assumption in the verification script itself (lcov 2.x's `FNA:`
  format vs. the older `FNDA:` this session's earlier ad hoc parsing
  script assumed).
- Remaining batches (vtable methods, registry lifecycle, internal
  helpers — 60 more zero-hit functions across ~17 files) tracked as
  follow-up work, not silently declared done.

---
### 51. Fix: branch coverage still not captured on real CI (v0.51.0)
---

Checking the *actual* v0.50.0 release run's logs (not assuming the
previous milestone's fix worked) found `branches...: no data found` —
the `--rc branch_coverage=1` flag added in milestone 50 had no effect
in real CI. Root cause: Ubuntu 22.04's `apt-get install lcov` provides
**lcov 1.15**, which predates the `branch_coverage` rc-key rename;
confirmed directly from lcov v1.15's own `lcovrc.5` man-page source
(`lcov_branch_coverage`, not `branch_coverage`) rather than guessing.
Fixed by passing both `--rc lcov_branch_coverage=1` (the name lcov 1.15
actually recognizes) and `--rc branch_coverage=1` (forward-compatible
with lcov 2.x) to every `lcov --capture`/`--remove` invocation in both
`ci.yml` and `release.yml`.
- This is the second time in this coverage-maximization effort that a
  fix looked right locally (my local lcov is 2.4) but needed
  verification against the real CI environment's actual tool version
  to catch — the same lesson as milestone 49's stale-c-FuSa-binary
  correction, applied again.

---
### 52. Coverage maximization, batch 2: decorator vtable methods (v0.52.0)
---

Second coverage batch: 16 of the 67 zero-hit functions were `zone()`/
`subscribe()`/`close()`/`send()` vtable methods on 8 decorator
controllers that simply delegate to an inner controller —
`e2e_ctrl_zone/subscribe/close`, `fi_ctrl_subscribe/close`,
`loan_ctrl_send/subscribe`, `observe_ctrl_zone/subscribe/close`,
`proxy_ctrl_subscribe`, `rl_ctrl_subscribe`,
`record_ctrl_zone/subscribe/close`, `redundancy_ctrl_subscribe`.
- **Mirrored the established `test_authz.c` pattern exactly**
  (`test_zone_delegates_to_inner`/`test_subscribe_delegates_to_inner`/
  `test_close_delegates_to_inner`) rather than inventing a new style —
  each new test asserts the delegated call reaches the *actual* inner
  controller (e.g. `close()`'s test sends through the inner directly
  afterward and expects `RCP_ERR_CLOSED`, proving the wrapper's close()
  really propagated rather than just returning `RCP_OK` locally).
  `faultinject`'s close test additionally confirms the wrapper itself
  starts rejecting `send()` after close, since `fi_ctrl_close()` (unlike
  the other seven) also flips an internal `closed` flag.
- **16 new requirements** added across 8 modules (`REQ-E2E-010..012`,
  `REQ-FI-009..010`, `REQ-LOAN-007..008`, `REQ-OBS-009..011`,
  `REQ-PROXY-007`, `REQ-RL-009`, `REQ-REC-009..011`, `REQ-RED-009`) —
  several modules (`faultinject`, `loan`, `ratelimit`, `redundancy`)
  already had `zone()` or `close()` tested from earlier milestones, so
  only the genuinely-missing methods got a new requirement rather than
  padding with redundant ones.
- **Verified real, not just added**: pulled the post-change lcov record
  for all 16 target functions and confirmed each now shows a non-zero
  hit count; local function coverage rose from 88.1% to 91.5%.
- Remaining batch: registry lifecycle (federation/shmem/tls/udp — the
  largest, ~44 functions across 4 files, including `udp.c`'s 60.7%
  line-coverage worst-in-codebase) and internal helpers (~7 functions),
  tracked as follow-up.

---
### 53. Coverage maximization, batch 3: registry lifecycle (v0.53.0)
---

The largest coverage batch: 30 zero-hit functions across the 4
transport/registry modules — `federation.c` (2), `shmem.c` (5),
`tls.c` (9), and `udp.c` (14, the file with the worst line coverage in
the codebase at 60.7%).
- **`federation.c`**: `controllers()`/`deregister()` — straightforward,
  mirrors the `test_authz.c`/batch-2 delegation-test style.
- **`shmem.c`**: `zone_server_close()`/`_ok()` (health-flag lifecycle),
  the subscription watcher's `remove_sub()` path (triggered by closing
  the controller while a subscription is active — waited for the
  background thread to actually complete the teardown rather than
  asserting immediately), and registry `controllers()`/`deregister()`.
- **`tls.c`**: confirmed via direct source read that this entire module
  is a compile-time stub (every `zone_server_*` function ignores its
  parameters and returns a fixed value, `registry.controllers()` always
  returns 0) — wrote one consolidated "stub surface is inert" test
  rather than 7 near-identical trivial tests, plus a real
  `controller.zone()` test and a `registry.controllers()` test.
- **`udp.c`** (the real socket-based transport, hence the largest and
  most involved of the four): `controller.zone()`, `addr_string()`
  (parses the real bound socket address), `set_healthy()` (verified it
  actually changes a subsequently-published Status, not just that it
  doesn't crash), and — the trickiest one — `same_addr()`/
  `subs_remove()`/`zserv_subs_remove()`, which only run when a
  subscription's status channel is closed *without* closing the whole
  controller (closing the controller takes a different code path that
  skips them entirely, confirmed by reading `udp_watcher_thread_fn`
  before writing the test). Also added registry lifecycle tests
  (`register`/`lookup`/`controllers`/`deregister`/`close`, all already
  covered by shared `REQ-REG-*` requirements from the mock registry's
  own tests — reused those IDs rather than inventing UDP-specific
  duplicates) and `rcp_udp_registry_new()`/`rcp_udp_registry_dial()`.
- **Noted, not fixed**: `tests/test_udp.c` uses an existing (pre-dating
  this batch) local convention of `//cfusa:req` tags directly above
  each test function rather than the `//cfusa:test` block-at-top style
  every other test file in this project uses. Matched that file's own
  established convention for the new tests rather than "fixing" an
  unrelated, out-of-scope inconsistency mid-batch.
- **21 new requirements** across the 4 modules (`REQ-FED-009..010`,
  `REQ-SHMEM-009..013`, `REQ-TLS-011..013`, `REQ-UDP-015..019`).
- **Verified real and non-flaky**: pulled the post-change lcov record
  for all 30 target functions — all show non-zero hits, none missing.
  Local function coverage rose from 91.5% to 96.4% (line: 85.5% →
  90.4%). The UDP socket/thread-teardown tests were additionally run 8
  times standalone (plain and under ASan/UBSan) with zero failures,
  given the inherent timing sensitivity of real-socket async teardown.
- Remaining batch: internal helpers (~7 functions: `prioqueue.c`'s heap
  operations, `sim.c`, `powerstate.c`, `mdns.c`, `relay.c`, `observe.c`'s
  gauge-recording functions), tracked as follow-up.

### 54. Coverage maximization, batch 4: internal helpers (v0.54.0)
---

The final batch of the coverage-maximization effort. The original
estimate of "~7 functions" undercounted: re-deriving the exact list from
the audit gave **14 zero-hit functions across 7 files**, all closed
here.
- **`prioqueue.c`**: `higher()`, `heap_swap()`, `heap_sift_down()` — the
  binary max-heap's internal comparison/reorder helpers, previously
  never exercised because the existing concurrency test's fast mock
  handler let the dispatch thread drain each entry before the next was
  even enqueued, so the heap never held more than one element at a time
  (confirmed by tracing `heap_pop()`: `heap_sift_down()` is only called
  when `heap_len > 0` *after* the decrement, i.e. only when 2+ entries
  were pending). Fixed with a deterministic (not racy) construction:
  one thread sends a first command into a handler that blocks on its
  first invocation, then — while that thread is still stuck inside the
  handler — three more commands are sent sequentially from the test's
  own thread using an already-expired context deadline. `pq_ctrl_send()`
  pushes onto the heap unconditionally before ever consulting the
  context, so each of those three calls deterministically enqueues (in
  a known order) before returning `RCP_ERR_TIMEOUT` almost immediately;
  the entries themselves stay queued for the dispatch thread to process
  later. Hand-simulating the resulting heap confirmed a real swap
  occurs during insertion (the third, `CRITICAL`-priority push bubbles
  past the first two via `heap_sift_up()`), so `heap_swap()` is
  guaranteed hit — not just probabilistically likely.
- **`sim.c`**: `sc_subs_remove()` (only reached when a status channel is
  closed directly rather than closing the whole controller — the same
  distinction discovered for UDP in batch 3, confirmed again here by
  reading `sim_watcher_thread_fn` before writing the test) and
  `rcp_sim_controller_watchdog_missed()` (tested across all three of its
  states: never kicked → `true`, freshly kicked → `false`, kicked but
  past the timeout → `true`, plus `watchdog_timeout_ms == 0` → always
  `false`).
- **`powerstate.c`**: `callbacks_append()` and
  `rcp_powerstate_manager_subscribe()` — the latter already carried a
  requirement tag from the original milestone but had never actually
  been called by a test. Added a test subscribing two callbacks (forcing
  `callbacks_append()`'s backing array to grow past one entry) and
  asserting both are invoked with the correct zone/state on a real
  sleep transition.
- **`mdns.c`**: `rcp_mdns_announcer_destroy()` — added a `destroyed`
  flag to the test file's existing `test_announcer_t` double (mdns.h
  ships no concrete Announcer implementation, matching cpp-RCP) and
  confirmed both dispatch-through-vtable and the NULL no-op case.
- **`relay.c`**: `rcp_relay_caller_retain()` and
  `relay_message_channel_is_closed()` — both live in `test_adapt.c`
  (there's no separate `test_relay.c`; RELAY-layer functions are tested
  there since `relay.c`/`adapt.c` implement the same conformance layer).
- **`observe.c`**: `rcp_span_duration_ms()` (a pure function, tested
  directly against a stack `rcp_span_t`) and `noop_record_gauge()`/
  `in_memory_record_gauge()` — neither is ever called internally
  (`observe_ctrl_send()` only calls `record_span()`/`record_counter()`
  on its sink; `record_gauge()` exists on the vtable for API
  completeness but has no internal call site), so both were called
  directly through the sink vtable.
- **`proxy.c`**: `proxy_registry_controllers()` — mirrors the
  `federation.c`/`shmem.c` registry-`controllers()` test pattern from
  batch 3.
- **9 new requirements** (`REQ-PQ-009`, `REQ-OBS-012..013`,
  `REQ-PWR-010`, `REQ-MDNS-009`, `REQ-RELAY-015..016`, `REQ-SIM-009..010`,
  `REQ-PROXY-008`).
- **Caught before shipping**: the first pass at the `prioqueue.c` test
  used `RCP_PRIORITY_LOW`, a priority level that doesn't exist in this
  project (only `NORMAL`/`HIGH`/`CRITICAL`) — a build failure, not a
  runtime bug, fixed before the first green build. Separately, the two
  new `test_adapt.c` tests were initially missing their `RUN_TEST(...)`
  registrations — caught because the local coverage build showed 0 hits
  for `rcp_relay_caller_retain()`/`relay_message_channel_is_closed()`
  despite the test functions compiling cleanly, a reminder that a test
  which compiles but is never registered produces exactly the same
  "function never called" signature as a genuinely missing test.
- **Verified real and non-flaky**: local function coverage rose from
  96.1% to **98.9%** (line: 89.7% → 92.5%); all 14 target functions
  confirmed via the actual lcov record showing non-zero hits, not just
  the aggregate percentage. The two timing-sensitive tests
  (`test_heap_reorders_multiple_pending_entries`,
  `test_closing_channel_directly_removes_watcher_subscription`) were run
  8× standalone plain and 8× under ASan/UBSan, zero failures.
- This completes the coverage-maximization effort begun by the user's
  "close the gaps now" request: all 67 originally-identified zero-hit
  functions now have tests, and branch-coverage instrumentation
  (milestones 50–51) is live in CI.

### 55. RELAY-conformance audit remediation, batch 2: mechanical fixes (v0.55.0)
---

A second, deeper conformance audit (distinct from the earlier 5-issue
batch fixed at milestones 44–48) filed 7 new issues (#55–#61). This
milestone closes the 4 mechanical/CI ones; #55 (Adapt() error-wrapping),
#56 (TARA content), and #57 (TLA+ specs) are larger and tracked as
follow-up milestones.
- **#59 — language field**: RELAY v1.12 (github.com/SoundMatt/RELAY PR
  #61) added `"c"` to `cli-version.json`'s `language` enum, resolving
  the upstream gap that forced this project to report `"cpp"` since
  milestone 47. Switched to the accurate `"c"` in both `version_json()`
  and `version_text()`, and bumped `RELAY_SPEC_VERSION` from `"1.11"` to
  `"1.12"` — confirmed via `git diff v1.11.1..v1.12.0 --stat` upstream
  that the language-enum addition is v1.12's only functional change, so
  no other conformance behavior shifts.
- **#61 — capabilities protocol fields**: `capabilities_json()` was
  missing `"protocol"`/`"protocol_int"`, which `version_json()` already
  carried and which §12.2's worked example includes for single-protocol
  tools. Added both, matching `version_json()`'s values.
- **#60 — module naming**: renamed `src/record.c`/`include/rcp/record.h`/
  `tests/test_record.c` to `recorder.c`/`recorder.h`/`test_recorder.c`
  per spec §13.7.2's module-name registry (`recorder` for capture/replay
  concerns). Renamed the public API prefix too (`rcp_record_t` →
  `rcp_recorder_t`, etc.) rather than just the filenames — a
  filename-only rename would leave the public symbol surface still
  saying "record", which doesn't meaningfully satisfy a module-naming
  requirement. Left `rcp_playback_*` as-is (already descriptively named
  for the replay half) and the internal `record_append()` helper renamed
  to `recorder_append()` for consistency. Confirmed via grep this module
  is self-contained (no external callers) before renaming.
- **#58 — CI hard-gate**: discovered by reading `cfusa`'s own
  `cmd_trace.c` that `--req-coverage N` already hard-gates *both* metric
  1 (requirement traceability) and metric 2 (function annotation
  density) via its own exit code — the existing CI's two-step dance
  (`--req-coverage 100 || true` followed by a separate shell script
  `grep`-parsing "Metric 2" out of the text output to hard-check it
  manually) was solving a problem the tool already solves natively, more
  simply, and for both metrics at once. Replaced both steps with the one
  bare invocation (no `|| true`, no parsing). Metric 1 has been
  genuinely at 100% (383/383) for several releases now that all 43
  roadmap modules are implemented, so this hard-gates real, already-true
  state rather than something aspirational.
- **Verified**: full rebuild + `ctest` (41/41), ASan/UBSan rebuild +
  `ctest` (41/41), fresh local `cfusa check --strict` (0 errors) and
  `cfusa trace --req-coverage 100` (exit 0, both metrics 100%) against a
  freshly rebuilt `cfusa` binary.

### 56. RELAY-conformance audit remediation, batch 3: Adapt() error wrapping (v0.56.0)
---

Closes issue #55 (P1): `adapter_send()`/`adapter_call()`/
`adapter_subscribe()`/`adapter_close()` returned native `rcp_errc_t`
values directly, with no way for a caller written against the generic
`relay::Caller`-equivalent contract to test `ec == RELAY_ERRC_CLOSED` in
a protocol-agnostic way, unmet against spec §5.2 / §17 requirement 3.
- **A genuine numeric hazard found while implementing the issue's own
  suggested fix, not just style disagreement**: the issue suggests an
  `rcp_errc_to_relay_errc()` translation "applied at the
  adapter_send/_call/_subscribe/_close boundary." Read literally (having
  those four functions *return* the translated `relay_errc_t` value in
  place of the native one), this collides catastrophically:
  `RELAY_ERRC_CLOSED` is numerically `0` — the exact same value
  `rcp_relay_caller_send()` (and every other `rcp_errc_t`-returning
  function in this codebase) already use for **success**
  (`RCP_OK == 0`, documented in `relay.h`'s own vtable comment as
  "RELAY_OK-equivalent 0 return"). Substituting the raw sentinel value
  into the return path would make a real "closed" *failure*
  indistinguishable from success for any caller checking
  `ec != RCP_OK` — turning an error into an apparent success, not a
  cosmetic gap. Other language bindings avoid this because
  `std::error_condition`/`errors.Is` are structured types where "no
  error" is distinct from "error code 0 in category X"; plain C has no
  such category machinery.
- **Fix**: implemented `rcp_errc_to_relay_errc(int rcp_ec, relay_errc_t
  *out)` as a **query function**, not a value substitution —
  `adapter_send()`/`_call()`/`_subscribe()`/`_close()` continue
  returning their real `rcp_errc_t` values unchanged (so `RCP_OK`
  safely stays `0`), and a caller wanting protocol-agnostic sentinel
  testing calls this function on the (non-`RCP_OK`) result instead of
  comparing the raw return value directly. This satisfies the substance
  of §5.2 (protocol-specific errors are recognizably equivalent to the
  four common sentinels) through a mechanism that's actually safe under
  this project's plain-int error-code convention, and is documented as a
  deliberate adaptation in both `adapt.h` and this entry rather than a
  silent deviation from the issue's literal suggested code.
- Maps `RCP_ERR_CLOSED` → `RELAY_ERRC_CLOSED` and `RCP_ERR_TIMEOUT` →
  `RELAY_ERRC_TIMEOUT` — the only two RCP-specific codes with a genuine
  RELAY common-sentinel equivalent. `RCP_OK` and every other RCP-specific
  code (`NOT_FOUND`, `ALREADY_EXISTS`, `BUSY`, `ZONE_MISMATCH`,
  `NOT_SUPPORTED`, `FORBIDDEN`) report no equivalence — they're
  legitimate protocol-specific conditions, not one of RELAY's four.
  RELAY's other two sentinels (`NOT_CONNECTED`, `PAYLOAD_TOO_LARGE`)
  have no corresponding RCP condition to map from.
- New `REQ-RELAY-017`, tested end-to-end: closing the wrapped controller
  then calling `rcp_relay_caller_send()` really does produce
  `RCP_ERR_CLOSED`, which really does translate to
  `RELAY_ERRC_CLOSED`; an already-expired context on
  `rcp_relay_caller_call()` produces `RCP_ERR_TIMEOUT` →
  `RELAY_ERRC_TIMEOUT`; and `RCP_OK`/`RCP_ERR_ZONE_MISMATCH`/
  `RCP_ERR_NOT_FOUND`/`RCP_ERR_FORBIDDEN` all correctly report no
  equivalence.
- **Verified**: full rebuild + `ctest` (41/41), ASan/UBSan rebuild +
  `ctest` (41/41), fresh local `cfusa check --strict` (0 errors) and
  `cfusa trace --req-coverage 100` (100%, both metrics) against a
  freshly rebuilt `cfusa` binary.

### 57. RELAY-conformance audit remediation, batch 4: real TARA content (v0.57.0)
---

Closes issue #56 (P1): `tara.md`/`tara.json` had been pure `cfusa tara`
placeholder skeletons (`"[describe asset]"`, `"[describe threat]"`, etc.)
since v0.1.0 across 56 releases, despite `CYBERSECURITY.md` §3 citing
them as a complete TARA covering 5 named threats.
- **`cfusa tara` confirmed to have no input-file mechanism**: read
  `cmd_tara.c` directly — it's a one-shot skeleton generator with no way
  to seed real asset/threat content that survives regeneration, unlike
  `cfusa hara show`, which renders from a hand-authored
  `.fusa-hara.json` that release.yml never touches. Since
  `release.yml` ran `cfusa tara` fresh on every tag and committed the
  result back to `main`, hand-populating the file without also removing
  that step would have had the very next release silently clobber it
  back to boilerplate.
- **A second false claim found and fixed while writing the TARA
  honestly**: `CYBERSECURITY.md`'s Layer 5 section claimed "SHA-256 hash
  of the transferred image is verified... before activation." Direct
  read of `src/firmware.c` found no hash/digest/checksum logic anywhere
  — `rcp_firmware_session_verify()` is purely a state-machine transition
  gated by a timeout, with no cryptographic check at all. Writing an
  honest TARA entry for the OTA-tampering threat was impossible without
  either repeating this false claim or contradicting it, so — with the
  user's explicit approval to expand scope for this — corrected
  `CYBERSECURITY.md`'s Layer 5 section and its threat/countermeasure
  table row to accurately describe `verify()` as a protocol-flow gate,
  not a cryptographic one, and filed the real gap as issue #69 (real
  image-hash verification is not yet implemented).
- Also softened `CYBERSECURITY.md`'s "formally verified" claim for the
  E2E anti-replay guard (Layer 3 text and its threat-table row) to
  reference issue #57 rather than assert a clean TLA+ proof that is
  itself under separate dispute — the TARA's own TS-002 entry is
  explicit that its MEDIUM risk rating rests on `test_e2e.c`'s tested
  runtime behavior, not on the disputed formal-verification claim.
- **The TARA itself** (`tara.json`/`tara.md`): 4 assets, 5 threats
  (matching `CYBERSECURITY.md`'s existing table), each with a real
  attack-vector/impact/feasibility/risk rating grounded in the actual
  code — not the aspirational architecture. The two HIGH-risk entries
  tied to identity (TS-001 command injection, TS-003 rogue controller
  registration) explicitly document that `tls.c` is a confirmed
  compile-time stub with no working backend in this library's own
  shipped posture, so real mitigation depends on an integrator supplying
  TLS — consistent with `CYBERSECURITY.md`'s own SEOOC framing of
  Layer 1, not a new finding invented for this TARA.
- **Stopped the clobbering**: removed the `cfusa tara` regeneration step
  and `tara.json`/`tara.md` from `release.yml`'s git-add list entirely.
  These files are now hand-maintained, exactly like
  `HARA.md`/`.fusa-hara.json` already were.
- **Verified**: full rebuild + `ctest` (41/41), fresh local
  `cfusa check --strict` (0 errors) and `cfusa trace --req-coverage 100`
  (100%, both metrics — this milestone changed no source/tests, only
  docs/JSON/CI) against a freshly rebuilt `cfusa` binary. Confirmed
  `tara.json` is valid JSON and `release.yml`'s YAML parses cleanly
  after the edits.

### 58. RELAY-conformance audit remediation, batch 5: real TLC verification (v0.58.0)
---

Closes issue #57 (P2), the last of the 7-issue second conformance
audit. `FORMAL_VERIFICATION.md` claimed the 3 TLA+ specs in `tla/`
model-check cleanly, but the audit found: no `.cfg` existed for any
spec, no CI job ever ran TLC, `AntiReplayGuard.tla` didn't even parse
for model-checking, and its `NoDoubleAccept` safety property was stated
backwards. **Actually did the formal verification, rather than just
downgrading the doc's claim** — downloaded `tla2tools.jar` (TLC2 2.19,
already present locally from the audit's own investigation) and ran it
for real, iterating until all three specs genuinely pass.
- **`AntiReplayGuard.tla`** (the spec with real bugs): `\E n \in Nat`
  is a non-enumerable quantifier bound — confirmed directly ("TLC
  encountered a non-enumerable quantifier bound Nat"). Bounded it to a
  new `MaxSeq` CONSTANT. `NoDoubleAccept` was
  `\A n \in accepted : n \notin accepted'` — backwards, since `accepted`
  only ever grows (`Check`'s `accepted' = accepted \cup {n}`); fails on
  the very first accept. Corrected to
  `\A n \in accepted : n \in accepted'` (monotonicity — combined with
  `Check(n)`'s own `n \notin accepted` precondition, this is what "no
  double acceptance" actually means for this model). First attempt at
  an *additional* invariant (`WindowInvariant`, asserting every accepted
  `n` stays within the current window forever) was itself wrong and
  caught by TLC — old accepted entries are *supposed* to age out of the
  window without being forgotten (that's how duplicate detection keeps
  working), so the invariant was dropped rather than kept and
  mis-explained.
- **`HealthStateMachine.tla`**: `miss_count` grew without bound (`Miss`
  incremented it forever, even after reaching `Faulted`), making the
  state space genuinely infinite — TLC would never terminate. Capped it
  at `MaxMiss` in the model itself, confirmed to be a model-checking-only
  bound with no behavioral difference (no C or TLA+ logic reads
  `miss_count` past the point it first reaches the fault threshold).
- **`WatchdogProtocol.tla`**: `clock` is a genuinely free-running counter
  with no fixed point (`Tick` always fires `clock' = clock + 1`) —
  bounded via a `ClockBound` state CONSTRAINT in the `.cfg`, the standard
  TLA+ idiom for this exact situation (doesn't touch the spec itself).
- **All three now genuinely pass**: re-ran each one after every fix,
  confirmed the real `Model checking completed. No error has been
  found.` output each time — not inferred from the corrected formulas,
  actually executed via `tla2tools.jar`.
- **Wired into CI**: new `formal-verification` job in `ci.yml`
  downloads `tla2tools.jar` from the official TLA+ releases and
  model-checks all three specs on every push/PR against `main`,
  failing the job on any violation.
- Updated `FORMAL_VERIFICATION.md` with a "Corrections" section
  documenting exactly what was wrong and what changed, and restored
  `CYBERSECURITY.md`'s "formally verified" claim for the E2E anti-replay
  guard (softened to "disputed" during issue #56's remediation, now
  genuinely true again) — including the TARA's own TS-002 entry, which
  had explicitly avoided relying on the disputed claim.
- This closes the last of the 7 issues from the second RELAY-conformance
  audit (#55–#61), plus the two issues (#69, TARA's OTA-tampering entry)
  discovered and fixed along the way. All are now closed.

---
### Phase 13 — Wire Format Core
---

Dependency root of the entire replacement program: nothing above the wire
layer can be built, tested, or even meaningfully designed before this
lands. Spec basis: OPEN Alliance TC18 RCP v0.5.1_RC, Ch.10–11 (extraction
§2).

### 59. AVTPDU framing (v0.59.0) ✅

- New `include/rcp/avtp.h` + `src/avtp.c`: the two IEEE 1722 AVTPDU header
  variants — NTSCF (`ntscf_data_length`, `sequence_num`; the *only* format
  an RC Server ever sends) and TSCF (`stream_data_length`,
  `avtp_timestamp`; client→server only, presentation-time semantics — the
  earliest execution time, never a hard deadline).
- `stream_id` (sender MAC + locally-assigned unique suffix) and
  `byte_bus_id` (endpoint-within-stream addressing, unique only *within*
  a given `stream_id`, not globally) as first-class addressing types,
  replacing `rcp_zone_t` everywhere.
- **Transport independence, explicit from day one**: an
  `rcp_avtp_transport_t` vtable abstraction with three concrete carriers
  targeted across this milestone and the next: native Ethernet
  (EtherType 1722), IEEE1722-over-UDP/IP (spec Annex J), and
  CAN(FD/XL)-as-underlying-network (extraction §2.1, §3.13) — the last of
  which is easy to miss and explicitly flagged in the gap analysis as a
  case this project's prior Ethernet-only assumption (`udp.c`, `tsn.c`)
  got wrong for the old protocol *and* would get wrong again here if not
  called out up front. A TSCF-headed AVTPDU received while the server
  doesn't support time-sync is dropped outright, no response — implemented
  and tested as an explicit, unconditional rule (extraction §2.2), not an
  incidental side effect of a missing feature.
- Terminology note for the codebase and its docs going forward: "RC
  System" / "RC Node" / "RC Client" / "RC Server" / "RC Edge Node" /
  "Endpoint (EP)" / "RCP Message" / "RCP Frame" replace "zone
  controller"/"HPC"/"Command"/"Response"/"Status" throughout.

**Done (v0.59.0)**: `include/rcp/avtp.h` + `src/avtp.c` land as new,
additive protocol-core surface — nothing in `rcp.h`/`wire.c` or any
satellite package is touched, so the legacy Zone/Command/Response/Status
stack keeps building unchanged alongside this new wire layer until its own
cutover milestone.
- NTSCF (`ntscf_data_length`/`sequence_num`, subtype `0x82`) and TSCF
  (`stream_data_length`/`avtp_timestamp`, subtype `0x05`) header codecs,
  both encode/decode round-tripping through a `rcp_bytes_t` frame exactly
  like `wire.c`'s existing convention, and both drawn from the public IEEE
  1722-2016 base-standard subtype registry (not the confidential TC18
  spec text) — decode rejects a wrong subtype, a frame shorter than the
  fixed header, and a declared payload length extending past the supplied
  buffer.
- `rcp_stream_id_t` (48-bit MAC + 16-bit unique suffix, with
  `rcp_stream_id_to_u64()`/`_from_u64()` matching the header's on-wire
  packing) and `rcp_byte_bus_id_t`, combined into `rcp_avtp_addr_t`
  (`stream_id` + `byte_bus_id`) as the new addressing pair every module
  built against this wire layer will use going forward.
- `rcp_avtp_transport_t`: a vtable abstraction (`send`/`recv`/`close`/
  `destroy`, mirroring `rcp_controller_t`'s own convention in `rcp.h`) so
  adding Ethernet/IEEE1722-over-UDP-IP/CAN(FD/XL) carriers later never
  requires touching the codec above it. Only one concrete carrier ships
  in this milestone: `rcp_avtp_loopback_transport_new()`, an in-process,
  bounded-FIFO reference implementation (mirroring `mock.h`'s role for
  `rcp_controller_t`) used to exercise the vtable contract — including
  `rcp_context_t`-deadline timeouts and post-`close()` rejection — without
  a real socket or CAN interface. Native Ethernet, IEEE1722-over-UDP/IP,
  and CAN(FD/XL)-as-network carriers remain unimplemented, as flagged as
  acceptable for this milestone by this Phase's own framing above.
- `rcp_avtp_should_drop_tscf()`: the TSCF-without-time-sync drop rule as
  its own directly-tested function, not folded into a receive loop.
- `tests/test_avtp.c` (28 cases): header round-trips for both variants,
  `stream_id`/`byte_bus_id` addressing (including that a shared
  `stream_id` with a different `byte_bus_id` is *not* the same address),
  the drop rule for all three (subtype × time-sync-support) combinations
  that matter, and the loopback transport's FIFO order, timeout, capacity,
  and post-close behavior.
- 20 new requirements (`REQ-AVTP-001`..`020`) added to `.fusa-reqs.json`;
  `cfusa trace --req-coverage 100` and the full `ctest` suite both stay
  green.

### 60. ACF message format + byte_message_info header (v0.60.0) ✅

- `include/rcp/acf.h` + `src/acf.c`: ACF_ABB (`acf_msg_type = 0x0E`, no
  timestamp field at all) and ACF_GBB (`0x0D`, 64-bit `message_timestamp`)
  message codecs sharing one `byte_message_info` header layout —
  `acf_msg_length`/`pad`/`mtv`/`byte_bus_id`/`evt`/`hs`/`cs`/
  `transaction_num`/`op`/`rsp`/`err`/`ms`/`read_size`-or-`segment_num`
  (extraction §2.3–2.4). This header is the single most load-bearing
  structure in the protocol — every endpoint type built in later phases
  reads/writes it identically.
- The four response semantics layered on top of the same header
  (Acknowledge / Write response / Read response / Error response,
  distinguished by `err`/`op`/`evt[3:0]`, extraction §2.8), plus the
  timestamp-validity folding rules (`mtv=0`-but-zeroed region ⇒ treat as
  untimed; the `tu` "uncertain" state folds the same way, extraction
  §2.6).
- **Standard request kind only** in this milestone (best-effort,
  unconditional, mandatory per extraction §3.1) — conditional requests,
  cancellation, and fragmentation are explicitly deferred to their own
  later phases (17, 20) rather than smuggled in here.
- `tests/test_acf.c`: header round-trip, ABB/GBB length differences,
  all four response-type identification rules, timestamp-folding cases.

**Done (v0.60.0)**: `include/rcp/acf.h` + `src/acf.c` land as new,
additive protocol-core surface layered on top of milestone 59's AVTPDU
framing — an ACF message is exactly what an NTSCF/TSCF frame's payload
carries. Nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`, or any
satellite package is touched.
- `byte_message_info`'s on-wire byte layout (which byte/bit each field
  occupies) is this implementation's own original engineering design —
  the confidential OPEN Alliance TC18 spec text is never reproduced here,
  only the field names, their high-level semantics, and the two
  `acf_msg_type` values are taken by reference. `pad`/`hs`/`cs`/`rsp`/`ms`
  round-trip unmodified (their real behavior belongs to conditional
  requests/sequencers and compound bundles, both out of this milestone's
  scope), mirroring the treatment `avtp.c`'s TSCF codec already gives its
  own `mr` field.
- `mtv` is modeled as a 3-state field (`RCP_ACF_MTV_UNTIMED` /
  `_VALID` / `_UNCERTAIN`) rather than two separate bits, specifically so
  the "untimed" and "uncertain (`tu`)" states naturally fold into the same
  `rcp_acf_gbb_is_timed() == false` treatment through one comparison
  rather than two ORed conditions. `rcp_acf_encode_gbb()` additionally
  forces the wire's `message_timestamp` region to all-zero whenever
  `mtv == RCP_ACF_MTV_UNTIMED`, regardless of what the caller's struct
  holds — the on-wire half of the "zeroed region" rule.
- `rcp_acf_classify_response()`: `err` takes priority (any `err`-set
  message is `RCP_ACF_RESP_ERROR` regardless of `op`), otherwise `op`
  selects `RCP_ACF_RESP_WRITE` / `RCP_ACF_RESP_READ` /
  `RCP_ACF_RESP_ACKNOWLEDGE`. `rcp_acf_hdr_ack_has_event()` further
  distinguishes a plain Acknowledge from one tagged with an asynchronous
  event via a nonzero `evt` nibble.
- `read_size`/`segment_num` deliberately share one on-wire byte
  (`read_size_or_segment_num`): which interpretation applies is a
  function of `op`, and fragmentation's own use of a segment counter is
  left unimplemented rather than smuggled into this milestone, per its
  own explicit scope limit above.
- `tests/test_acf.c` (29 cases): `byte_message_info` header round-trip
  for both ABB and GBB (including every flag/count field, not just the
  ones with behavior), the exact 8-byte length difference between the
  two variants, all four response-type identification rules (including
  the ack-has-event sub-rule), and the timestamp-folding cases —
  `mtv=UNTIMED` forcing a zeroed wire region, and `mtv=UNCERTAIN`
  (`tu`) folding to "not timed" while still preserving its wire value.
- 15 new requirements (`REQ-ACF-001`..`015`) added to `.fusa-reqs.json`;
  `cfusa trace --req-coverage 100` (both metrics), `cfusa check`/`lint`/
  `analyze`/`cyber` (0 errors), the full `ctest` suite (43/43), and
  `relay conform --strict` all stay green.

---
### Phase 14 — RC Server Lifecycle & Register Map
---

Spec basis: Ch.12 (extraction §3). The gap analysis flags the 3-state
lifecycle as "probably the single biggest structural gap" relative to the
old informal protocol — expect this to touch register-access code paths
pervasively for the rest of the program.

### 61. Lifecycle state machine (v0.61.0) ✅

- `include/rcp/server.h` + `src/server.c`: `rcp_server_lifecycle_t` with
  the three states `HW_UNCONFIGURED` (`0x00`), `HW_CONFIGURED` (`0x55`),
  `RCP_CONFIGURED` (`0xAA`), and their transition guards — the
  `HW_CFG_INCONSISTENT` plausibility check (every `ep_used=1` endpoint has
  a valid HW pin mapping and at least one configured request stream) gating
  entry to `HW_CONFIGURED`, and the `RCP_CFG_INCONSISTENT` check (every used
  endpoint has a stream/`byte_bus_id` association, every request stream has
  an associated response stream) gating entry to `RCP_CONFIGURED`.
- Per-state request filtering: `HW_UNCONFIGURED` accepts only ACF_ABB at
  discovery `byte_bus_id` 0 under NTSCF (everything else silently dropped,
  TSCF dropped outright); demotion back to `HW_UNCONFIGURED` from
  `HW_CONFIGURED` via the same discovery-stream/root-client path.
- Register-locking-by-state: HW-pin/generic config read-only once
  `HW_CONFIGURED`; functional config the only thing left writable in
  `RCP_CONFIGURED`, and only via the endpoint's own registered stream(s)
  or the root client through EP0. `W*` (permanently locked once
  `RCP_CONFIGURED`) vs. plain `W` fields modeled explicitly, not
  collapsed into one writability bit.
- Per-endpoint `ep_enable`: disabled endpoints still queue requests
  without executing them (pre-load-then-drain-on-enable semantics).

**Done (v0.61.0)**: `include/rcp/server.h` + `src/server.c` land as new,
additive protocol-core surface layered on top of milestone 59's AVTPDU
framing and milestone 60's ACF message format. Nothing in `rcp.h`,
`wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`, or any satellite package is
touched.
- `rcp_server_lifecycle_t` (`HW_UNCONFIGURED`/`HW_CONFIGURED`/
  `RCP_CONFIGURED` = `0x00`/`0x55`/`0xAA`) plus `rcp_server_errc_t`
  (`RCP_SERVER_ERR_HW_CFG_INCONSISTENT`/`_RCP_CFG_INCONSISTENT`/
  `_INVALID_TRANSITION`). The two plausibility checks
  (`rcp_server_check_hw_cfg()`/`_check_rcp_cfg()`) run over a minimal,
  self-contained `rcp_server_plausibility_snapshot_t` stand-in
  (per-endpoint `ep_used`/`hw_pin_mapped`/`has_request_stream`/
  `has_stream_assoc`, per-request-stream `configured`/
  `has_response_stream`) — deliberately not milestone 62's full register
  tables, per this milestone's own scope limit above. A `NULL` snapshot is
  treated as inconsistent (fail-safe), never as vacuously plausible.
- `rcp_server_lifecycle_transition()`: gates `HW_UNCONFIGURED` →
  `HW_CONFIGURED` and `HW_CONFIGURED` → `RCP_CONFIGURED` behind the two
  plausibility checks, leaving state unchanged and returning the specific
  `*_INCONSISTENT` code on failure; treats `HW_CONFIGURED` →
  `HW_UNCONFIGURED` and `RCP_CONFIGURED` → `HW_UNCONFIGURED` (the
  discovery-stream/root-client reset path) as unconditional; treats a
  same-state target as a no-op success; rejects every other transition
  (e.g. skipping straight to `RCP_CONFIGURED`, or downgrading from
  `RCP_CONFIGURED` to `HW_CONFIGURED` directly) with
  `RCP_SERVER_ERR_INVALID_TRANSITION`.
- `rcp_server_lifecycle_should_accept()`: the per-state request-filtering
  rule as its own directly-tested function, mirroring `avtp.c`'s
  `rcp_avtp_should_drop_tscf()` convention and reusing that function for
  the ordinary TSCF/time-sync rule. While `HW_UNCONFIGURED`, a TSCF-headed
  frame is dropped outright regardless of time-sync support (presentation
  time presupposes a configured request stream that cannot exist yet), and
  only an NTSCF-headed ACF_ABB request at `RCP_SERVER_DISCOVERY_BYTE_BUS_ID`
  is accepted — everything else is silently dropped. While `HW_CONFIGURED`/
  `RCP_CONFIGURED`, frame-level acceptance beyond the time-sync rule is
  unrestricted at this milestone.
- `rcp_server_field_writable()`: register-locking-by-state over
  `rcp_server_field_kind_t` — `HW_GENERIC` (writable only in
  `HW_UNCONFIGURED`), `FUNCTIONAL_W` (writable by any writer while
  `HW_CONFIGURED`, restricted to `rcp_server_writer_ctx_t`'s
  `via_root_client_ep0`/`via_owning_stream` once `RCP_CONFIGURED`), and
  `FUNCTIONAL_W_STAR` (same through `HW_CONFIGURED`, but permanently
  locked for every writer once `RCP_CONFIGURED`) — three distinct enum
  values, not one writability bit, per the roadmap's explicit requirement.
- `rcp_server_endpoint_t` + `rcp_server_endpoint_submit()`/
  `_set_enable()`/`_drain_one()`/`_queue_len()`: pre-load-then-drain-on-
  enable semantics for per-endpoint `ep_enable` — a disabled endpoint
  queues (rather than executes) a submitted request; `drain_one()` refuses
  while still disabled and otherwise dequeues in FIFO order once
  re-enabled.
- `tests/test_server.c` (36 cases): lifecycle wire values; both
  plausibility checks in triggering and non-triggering form (including the
  `NULL`-snapshot fail-safe); every modeled and rejected lifecycle
  transition; per-state request filtering (discovery accept, silent drop,
  TSCF-outright-drop while `HW_UNCONFIGURED`; the ordinary time-sync rule
  once configured); register-locking for `HW_GENERIC`/`FUNCTIONAL_W`/
  `FUNCTIONAL_W_STAR`; and `ep_enable`'s queue-then-drain-on-enable
  behavior, including FIFO ordering and destroying a non-empty queue.
- 24 new requirements (`REQ-SRV-001`..`024`) added to `.fusa-reqs.json`;
  `cfusa trace --req-coverage 100` (both metrics), `cfusa check`/`lint`/
  `analyze`/`cyber` (0 errors), the full `ctest` suite (44/44, including
  under ASan/UBSan), and `relay conform --strict` all stay green.

### 62. Register-map model (v0.62.0) ✅

- The general (vendor-agnostic, EP0-reachable) register map: magic
  number, `svr_version`, vendor/device ID, `svr_ep_count`,
  stream/sequencer/memory capacity fields, `svr_implemented_options`
  bitmask (time-sync / enhanced-cancel / compound bundles — each an
  all-or-nothing group per extraction §3.1), and the pointer/capacity
  pairs to every sub-table (HW pin map, request-stream config,
  response/ack queue config, EP generic/functional config,
  EP-ID/`byte_bus_id` map, sequencer state) — extraction §3.6.
- **The generic-vs-functional config split**, itself flagged as a
  recent, pervasive TC18 restructuring (extraction §6 item 5): the
  server-owned generic block (`ep_type`, `ep_used`, `ep_delay_time`,
  `ep_req_storage_size`, ...) vs. the common functional-config prefix
  shared by every endpoint type (`ep_enable`, `ep_clear_req_storage`,
  `ep_req_crc_enable`, `ep_response_ts_enable`, `ep_supress_response`,
  ...) — modeled as two distinct C structs, not one merged blob, so every
  endpoint type built in Phase 16/19 composes them the same way.
- EP0 itself as the pseudo-endpoint exposing this whole surface;
  `svr_root_client_index` (the one stream with full-server write access)
  and the per-EP-restricted-client model for every other stream.
- HW pin-mapping table (`hw_ep_nr`/`hw_ep_pin_nr`/pin-property byte) and
  the full per-endpoint-type named-signal index table (GPIO 0–31, SPI
  CLK/PICO/POCI/CS0-5, I²C SCL/SDA, ... extraction §3.7) — written once
  here, reused unmodified by every endpoint type added later.
- Request-stream config (`rx_stream_id`, `rx_wd_*`, `rx_enforce_e2e`,
  `rx_safety_measure`, ...) and response/ack queue config
  (`Max_AVTPDUsize`, `flush_on_count`, `Flush_time`) tables — fields
  relevant to E2E/watchdog (Phase 18) are modeled now but not yet wired
  to behavior, avoiding a second register-layout pass later.
- **Known spec ambiguity carried forward as an explicit code comment,
  not silently resolved**: the EP-ID/`byte_bus_id` mapping table's
  required ascending ordering has no server-side enforcement per the
  spec itself (extraction §3.9, §7) — documented as a client
  responsibility with no corrective mechanism, not treated as a bug in
  this implementation.

**Done (v0.62.0)**: `include/rcp/regmap.h` + `src/regmap.c` land as new,
additive protocol-core surface layered on top of milestone 59's AVTPDU
framing, milestone 60's ACF message format, and milestone 61's lifecycle
state machine. Nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`,
`acf.h`/`acf.c`, `server.h`/`server.c`, or any satellite package is
touched.
- `rcp_regmap_general_t`: magic/`svr_version`/vendor/device ID/
  `svr_ep_count`/stream/sequencer/memory capacity fields plus
  `svr_root_client_index` and an `rcp_regmap_table_ref_t`
  (offset/capacity) pair for each of the six sub-tables below;
  `rcp_regmap_general_init()` zero-initializes it with
  `svr_root_client_index` defaulted to the new `RCP_REGMAP_NO_ROOT_CLIENT`
  sentinel. `svr_implemented_options`'s three feature groups (time-sync/
  enhanced-cancel/compound-bundles) are this implementation's own bit
  assignment; `rcp_regmap_options_group_consistent()` is the all-or-
  nothing-per-group check the roadmap called for.
- The generic-vs-functional split landed as two distinct structs exactly
  as scoped: `rcp_regmap_ep_generic_cfg_t` (server-owned: `ep_type`,
  `ep_used`, `ep_delay_time`, `ep_req_storage_size`) and
  `rcp_regmap_ep_functional_cfg_t` (the common prefix: `ep_enable`,
  `ep_clear_req_storage`, `ep_req_crc_enable`, `ep_response_ts_enable`,
  `ep_suppress_response`), each with its own zero-initializer.
- `RCP_REGMAP_EP0_INDEX` (0) is deliberately the same numeric value as
  `RCP_SERVER_DISCOVERY_BYTE_BUS_ID` from milestone 61.
  `rcp_regmap_ep_client_t` models the per-endpoint owning-stream
  restriction, and `rcp_regmap_writer_ctx()` derives server.h's
  `rcp_server_writer_ctx_t` from `svr_root_client_index` and an
  endpoint's `rcp_regmap_ep_client_t` — reusing
  `rcp_server_field_writable()`'s existing authorization logic rather
  than duplicating it.
- `rcp_regmap_hw_pin_map_entry_t` (`hw_ep_nr`/`hw_ep_pin_nr`/
  `pin_property`, this module's own bit layout) plus
  `rcp_regmap_named_signal_t`, the 43-entry named-signal index (GPIO0-31,
  SPI_CLK/PICO/POCI/CS0-5, I2C_SCL/SDA) and its
  `rcp_regmap_named_signal_string()`.
- `rcp_regmap_request_stream_cfg_t` (`rx_stream_id`, `rx_wd_timeout_ms`,
  `rx_wd_action`, `rx_enforce_e2e`, `rx_safety_measure`) and
  `rcp_regmap_response_queue_cfg_t` (`max_avtpdu_size`,
  `flush_on_count`, `flush_time_us`) — the E2E/watchdog-relevant fields
  exist but no code reads them yet, per this milestone's explicit scope.
- `rcp_regmap_ep_id_map_entry_t` plus
  `rcp_regmap_ep_id_map_is_ascending()`, documented in both the file
  header and the function's own comment as a read-only diagnostic only —
  it is not called from, and must not be mistaken for, server-side
  enforcement, since the specification itself defines none.
- No encode/decode pair is added in this milestone (unlike avtp.h/acf.h):
  register *contents* are modeled now so every later endpoint type
  composes them the same way, but wiring them to an actual
  `byte_message_info` read/write exchange remains later phases' job.
- `tests/test_regmap.c` (25 cases): EP0 index identity, `general_init()`
  defaults, all three option-group consistency rules, all four
  `writer_ctx()` grant/deny combinations, pin-property and
  `svr_implemented_options` bit distinctness, named-signal-string
  non-NULL/uniqueness, every config struct's zero-initializer, and the
  ascending-table diagnostic's true/false/vacuous cases.
- 22 new requirements (`REQ-RMAP-001`..`022`) added to `.fusa-reqs.json`;
  `cfusa trace --req-coverage 100` (both metrics), `cfusa check`/`lint`/
  `analyze`/`cyber`/`vuln`/`qualify` (0 errors), and the full `ctest`
  suite (45/45, including under ASan/UBSan) all stay green.

---
### Phase 15 — Discovery
---

Spec basis: extraction §3.5. Flagged as "newly clarified" and tightly
wound into lifecycle bootstrapping — the second-highest-impact structural
gap after Phase 14 per the gap analysis's own prioritization (extraction
§9 item 2).

### 63. Discovery (v0.63.0) ✅

- Broadcastable ACF_ABB read request at `byte_bus_id 0`, register-map
  address 0, answerable in **any** lifecycle state, NTSCF-only (a
  TSCF-headed discovery request is dropped).
- Discovery-stream claiming: the first discovery request received while
  in `HW_UNCONFIGURED`/`HW_CONFIGURED` reserves the discovery stream for
  configuration writes; the reservation lapses after a configurable
  `Discovery_TimeOut` (~20 ms default) with no follow-up configuration
  request, re-opening the server to a new claimant. Concurrent read-only
  discovery from other clients remains possible while a stream holds the
  claim; only the claimant may configure.
- Response shape: register-map slice starting at address 0, length =
  requested `read_size`, addressed back to the requester's MAC,
  untagged — carrying the magic number, protocol version, vendor/device
  ID, and endpoint count needed for generic compatible-device recognition
  (vendor/device-specific detail is explicitly out-of-band, e.g. a
  datasheet).
- Client-side discovery result persistence (so re-discovery isn't
  mandatory every power cycle on a known topology) as a thin convenience
  API, not a protocol requirement.

**Done (v0.63.0)**: `include/rcp/discovery.h` + `src/discovery.c` land as
new, additive protocol-core surface layered on top of the AVTPDU framing
(milestone 59), ACF message format (milestone 60), RC Server lifecycle
state machine (milestone 61), and register-map model (milestone 62).
Nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`,
`server.h`/`server.c`, `regmap.h`/`regmap.c`, or any satellite package is
touched.
- `rcp_discovery_should_drop()`: the NTSCF-only rule as its own directly-
  testable function, mirroring `avtp.c`'s own
  `rcp_avtp_should_drop_tscf()` convention — a discovery request/response
  never rides on a TSCF-headed frame, independent of lifecycle state or
  time-sync capability. `rcp_discovery_decode_request()`/
  `_decode_response()` apply it before any ACF-level parsing is attempted.
- `rcp_discovery_encode_request()`/`_decode_request()`: a full NTSCF-
  framed ACF_ABB read addressed to `RCP_SERVER_DISCOVERY_BYTE_BUS_ID`,
  answerable regardless of `rcp_server_lifecycle_t` -- this module
  deliberately never gates on it, unlike ordinary field writes.
- `rcp_discovery_encode_response()`/`_decode_response()`: a register-map
  slice from address 0 whose payload length always equals the requested
  `read_size` (`RCP_DISCOVERY_GENERAL_SLICE_LEN`, this module's own
  12-octet layout, holds the populated magic/`svr_version`/vendor/device
  ID/`svr_ep_count` fields already modeled by `rcp_regmap_general_t`; any
  requested length beyond that is zero-filled reserved space). Addressing
  the underlying carrier frame back to the requester's MAC is left to a
  future real transport, matching `avtp.h`'s transport-independent design
  -- the requester's `stream_id.mac`, already recovered by
  `rcp_discovery_decode_request()`, is what such a transport would use.
- `rcp_discovery_claim_t`: the discovery-stream claim/timeout/re-open
  state machine as its own small piece of server-side mutable state, with
  `Discovery_TimeOut` a configurable field (`RCP_DISCOVERY_DEFAULT_TIMEOUT_MS`
  = 20 ms is only a suggested default, mirroring `regmap.h`'s
  `rcp_regmap_request_stream_cfg_t.rx_wd_timeout_ms` convention of never
  hardcoding a timeout inside the module itself).
  `rcp_discovery_claim_note_request()` grants an open (never-held or
  lapsed) claim to its requester without preempting an active claimant;
  `rcp_discovery_claim_note_config_write()` refreshes the claimant's
  deadline and never resurrects an already-lapsed claim;
  `rcp_discovery_claim_is_claimant()` is the pure query a caller combines
  with `rcp_server_field_writable()`/`rcp_regmap_writer_ctx()` -- reusing
  their existing authorization logic rather than duplicating it, the same
  pattern `regmap.c` established for milestone 62 -- as one more input to
  an overall write-authorization decision; `rcp_discovery_claim_release()`
  is the unconditional reset path for the server's demotion back to
  `HW_UNCONFIGURED`. Every function taking elapsed time takes an explicit
  `now_ms` parameter rather than reading a clock itself, keeping the whole
  state machine deterministic to test.
- `rcp_discovery_cache_t`: client-side discovery result persistence,
  keyed by each result's `server_stream_id` (one entry per server,
  last-discovered-wins), landed exactly as scoped -- a thin convenience
  API this module never itself consults to decide whether to (re-)issue a
  discovery request.
- `tests/test_discovery.c` (31 cases): the NTSCF-only rule across all
  three subtype cases, request/response encode-decode round trips, every
  request/response rejection path (wrong subtype, message type,
  byte_bus_id, op, short/empty frame), the response's read_size-always-
  matches-payload-length rule with both truncation and zero-fill, the
  full claim state machine (grant/no-preempt/lapse/reopen/refresh/reject/
  release), and the discovery cache's put/find/grow/update-in-place
  behavior.
- 24 new requirements (`REQ-DISC-001`..`024`) added to `.fusa-reqs.json`;
  `cfusa trace --req-coverage 100` (both metrics), `cfusa check`/`lint`/
  `analyze`/`cyber`/`vuln`/`qualify` (0 errors), and the full `ctest`
  suite all stay green.

---
### Phase 16 — Basic Endpoints
---

Spec basis: Ch.13.7 (extraction §5). Ordered simplest-shape-first per this
effort's own scope: GPIO and SPI (fixed small payloads, no averaging/timing
state) before I²C/UART (raw byte streams) before ADC/PWM (multi-field,
stateful averaging/timing). DAC is **explicitly out of scope for this
program** — enumerated in the spec's `ep_type` table with a `DAC_OUT` pin
signal, but with no functional-config chapter anywhere in v0.5.1_RC
(extraction §1.2, §7); treated as reserved-but-undefined and revisited only
if a future spec revision actually defines it.

### 64. GPIO endpoint (v0.64.0) ✅

- `include/rcp/ep_gpio.h` + `src/ep_gpio.c`: up to 32 pins, 4-byte
  bitmask request/response payload, all 8 `evt[2:0]` write-semantics
  (replace/OR/AND/XOR/add/subtract, with add/subtract saturating rather
  than wrapping at `0x0000`/`0xFFFF`, plus the reconfiguration escape
  hatch at `evt[2:0]=7`, extraction §4.5 Group C).
- Per-pin trigger signals (any-change/rising/falling) and functional
  config (direction, electrical properties mirroring the HW pin-mapping
  table's pull-up/down/drive-strength fields for runtime adjustment).

**Done (v0.64.0)**: `include/rcp/ep_gpio.h` + `src/ep_gpio.c` land as new,
additive protocol-core surface layered on top of the AVTPDU framing
(milestone 59), ACF message format (milestone 60), RC Server lifecycle
state machine (milestone 61), register-map model (milestone 62), and
discovery (milestone 63). Nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`,
`acf.h`/`acf.c`, `server.h`/`server.c`, `regmap.h`/`regmap.c`,
`discovery.h`/`discovery.c`, or any satellite package is touched.
- Unlike discovery, a GPIO request/response is ordinary endpoint traffic
  with no NTSCF-only rule of its own, so this module operates at the ACF
  level only (`acf.h`'s `rcp_acf_encode_abb()`/`_encode_gbb()` and their
  decode counterparts) -- a caller wraps/unwraps NTSCF or TSCF framing
  separately via `avtp.h`, exactly as `acf.c` itself does not wrap AVTP.
  `rcp_ep_gpio_encode_read_request()`/`_decode_read_request()` and
  `_encode_write_request()`/`_decode_write_request()` round-trip the
  4-byte big-endian bitmask payload against a caller-supplied
  `byte_bus_id`; `rcp_ep_gpio_encode_response()`/`_decode_response()`
  choose ACF_ABB (untimed) or ACF_GBB (timed, carrying
  `message_timestamp`) per a caller-supplied `timed` flag standing in for
  the endpoint's `ep_response_ts_enable` functional-config bit.
- `rcp_ep_gpio_apply_write()`: the six ordinary evt[2:0] data-write
  semantics (replace/OR/AND/XOR/add/subtract) against the endpoint's
  32-bit bitmask register, with add/subtract saturating at
  `0x00000000`/`0xFFFFFFFF` rather than wrapping -- this module's own
  extension, to GPIO's register width, of the write-semantics saturation
  rule described generically (at a narrower word width) for other
  endpoint types' fields. evt[2:0]=6 is not otherwise assigned meaning by
  this milestone's scope and is implemented as a documented no-op pending
  spec clarification, flagged rather than guessed at (mirroring this
  project's existing convention for a genuine spec ambiguity, e.g.
  `regmap.h`'s EP-ID/`byte_bus_id` ordering note).
- `rcp_ep_gpio_apply_reconfig()`: the reconfiguration escape hatch
  (evt[2:0]=7) -- this module's own original design for what it
  accomplishes, reinterpreting the 32-bit write payload as a per-pin
  selector and toggling each selected pin's direction
  (`RCP_REGMAP_PIN_PROP_OUTPUT`/`_INPUT`, `regmap.h`) while leaving every
  other `pin_property` bit, and every unselected pin, untouched.
- `rcp_ep_gpio_trigger_fires()`: the three per-pin trigger modes
  (any-change/rising/falling) plus `NONE`, evaluated as a pure function of
  one level transition.
- `rcp_ep_gpio_functional_cfg_t` composes `regmap.h`'s
  `rcp_regmap_ep_functional_cfg_t` as its own first member (per that
  module's documented convention) and adds one `pin_property`
  (`RCP_REGMAP_PIN_PROP_*`, mirroring the HW pin-mapping table's own
  direction/pull-up/pull-down/drive-strength fields, independently
  runtime-adjustable here) plus trigger mode per pin.
  `rcp_ep_gpio_functional_cfg_writable()` is a thin, named wrapper over
  `rcp_server_field_writable()` (`RCP_SERVER_FIELD_FUNCTIONAL_W`) --
  reused, not duplicated, from `server.h`; `rcp_ep_gpio_set_pin_property()`/
  `_set_pin_trigger()` consult it (and pin-index validity) before ever
  mutating the config, the first actual caller of
  `rcp_server_field_writable()` in this codebase.
- `tests/test_ep_gpio.c` (40 cases) and 32 new requirements
  (`REQ-GPIO-001`..`032`) added to `.fusa-reqs.json`; `cfusa trace
  --req-coverage 100` (both metrics), `cfusa check`/`lint`/`analyze`/
  `cyber`/`vuln`/`qualify` (0 errors), and the full `ctest` suite all stay
  green.

### 65. SPI endpoint (v0.65.0) ✅

- `include/rcp/ep_spi.h` + `src/ep_spi.c`: controller-only, up to 6
  pre-configured channels selected via `evt[2:0]` values 0–5 (extraction
  §4.5 Group A); transfer payload is raw PICO-out bytes, response is the
  same-length POCI-in bytes.
- Per-channel functional config (clock polarity/phase — the 4 standard
  SPI modes, bit order, clock divider, CS active-polarity, inter-transfer/
  inter-byte timing) and transfer-done/per-CS-edge triggers.
- Compound-wait-against-SPI's truncation rule (compares only the first 4
  of up to 20 status bytes, extraction §4.6) implemented and tested even
  though compound-wait itself lands in Phase 17 — the truncation behavior
  is an SPI-endpoint property, tested here in isolation via a raw
  comparison-mode helper.

**Done (v0.65.0)**: `include/rcp/ep_spi.h` + `src/ep_spi.c` land as new,
additive protocol-core surface, following `ep_gpio.h`/`ep_gpio.c`'s
(milestone 64) established layering discipline exactly: ACF-level only
(`acf.h`'s `rcp_acf_encode_abb()`/`_encode_gbb()` and their decode
counterparts), with nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`,
`acf.h`/`acf.c`, `server.h`/`server.c`, `regmap.h`/`regmap.c`,
`discovery.h`/`discovery.c`, `ep_gpio.h`/`ep_gpio.c`, or any satellite
package touched.
- Unlike GPIO's fixed-shape 4-byte bitmask, an SPI transfer's payload is
  raw and variable-length: `rcp_ep_spi_encode_transfer_request()`/
  `_decode_transfer_request()` round-trip the PICO-out bytes as an
  `ACF_OP_WRITE` request, and `rcp_ep_spi_encode_response()`/
  `_decode_response()` round-trip the same-length POCI-in bytes as an
  `ACF_OP_READ`-classified response (`ACF_ABB` untimed / `ACF_GBB` timed,
  mirroring GPIO's own timed/untimed choice) — decoded payloads are
  *borrowed* views into the caller's frame buffer, matching `acf.c`'s own
  decode convention, since a variable-length payload has no natural
  fixed-size out-parameter to copy into. Channel selection also diverges
  from GPIO: `evt[2:0]` carries the channel number (0–5) directly rather
  than a write-semantics code, validated on decode via
  `rcp_ep_spi_channel_valid()` (`RCP_EP_SPI_ERR_BAD_CHANNEL` for 6/7).
- `rcp_ep_spi_mode_cpol()`/`_mode_cpha()`: pure, directly-testable
  derivation of the two underlying clock-polarity/-phase bits from each of
  the four standard `rcp_ep_spi_mode_t` values.
- `rcp_ep_spi_trigger_fires()`: this endpoint type's own analogue of
  `rcp_ep_gpio_trigger_fires()`, evaluating one of three SPI-specific
  events (transfer-done, CS-assert-edge, CS-deassert-edge) against a
  selected trigger mode.
- `rcp_ep_spi_functional_cfg_t` composes `regmap.h`'s
  `rcp_regmap_ep_functional_cfg_t` as its own first member (per that
  module's documented convention, same as `ep_gpio.h`) and adds, per
  channel: clock mode, bit order, CS active-polarity, clock divider,
  inter-byte/inter-transfer timing delays, and trigger mode.
  `rcp_ep_spi_functional_cfg_writable()` is a thin, named wrapper over
  `rcp_server_field_writable()` (`RCP_SERVER_FIELD_FUNCTIONAL_W`) --
  reused, not duplicated, from `server.h`; every
  `rcp_ep_spi_set_channel_*()` mutator consults it (and channel validity)
  before ever mutating `cfg`.
- `rcp_ep_spi_compound_wait_status_equal()`: the SPI-truncation precedent
  called for by this milestone's scope, implemented and unit-tested now
  even though generic compound-wait request-type plumbing itself lands at
  Phase 17 milestone 69 -- compares only the first
  `RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN` (4) of up to
  `RCP_EP_SPI_STATUS_MAX_LEN` (20) status bytes, byte for byte, returning
  false (fail-safe, never an error code) when either buffer is shorter
  than 4 bytes. Milestone 67's PWM_IN numeric compound-wait comparison
  modes are explicitly told to follow this precedent.
- `tests/test_ep_spi.c` (39 cases) and 32 new requirements
  (`REQ-SPI-001`..`032`) added to `.fusa-reqs.json`; `cfusa trace
  --req-coverage 100` (both metrics), `cfusa check`/`lint`/`analyze`/
  `cyber`/`vuln`/`qualify` (0 errors), and the full `ctest` suite
  (ASan/UBSan-clean) all stay green.

### 66. I²C + UART endpoints (v0.66.0) ✅

- `include/rcp/ep_i2c.h`/`ep_uart.h` + `.c`: I²C controller-only, raw byte
  stream including address bytes (no protocol-level address parsing);
  `i2c_mode` bus-speed presets, with the spec's own high-speed enum
  ambiguity (extraction §5.7, §7) implemented per the *lower*-numbered,
  more-conservative interpretation and flagged in a code comment pending
  spec errata, not silently guessed at.
- UART: independent TX/RX queues sharing one functional-config block, RX
  FIFO sized via `ep_rx_buffer_size`, `read_size`-or-`uart_timeout`
  completion race (short reads fragment via `ms` — exercised here only as
  a single-AVTPDU-worst-case test until Phase 20 lands full fragmentation),
  bit-padding for non-multiple-of-8 `uart_nr_bits`, and the payload-bearing
  UART read rejection (`UNKNOWN_CMD`) called out as a deliberate asymmetry
  versus GPIO/PWM_OUT in code comments.
- **Explicit LIN/CAN-adjacent scope validation, done here early rather
  than assumed**: I²C's raw-byte-stream, no-framing-help design (extraction
  §5.7) is the same philosophy this program commits to for LIN (Phase 19)
  — validated against this project's actual endpoint code once, not
  re-litigated per endpoint type.

**Done (v0.66.0)**: `include/rcp/ep_i2c.h` + `src/ep_i2c.c` and
`include/rcp/ep_uart.h` + `src/ep_uart.c` land as new, additive
protocol-core surface, following `ep_gpio.h`/`ep_spi.h`'s (milestones 64–65)
established layering discipline exactly: ACF-level only (`acf.h`'s
`rcp_acf_encode_abb()`/`_encode_gbb()` and their decode counterparts), with
nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`,
`server.h`/`server.c`, `regmap.h`/`regmap.c`, `discovery.h`/`discovery.c`,
`ep_gpio.h`/`ep_gpio.c`, `ep_spi.h`/`ep_spi.c`, or any satellite package
touched.
- I²C addresses exactly one bus per `byte_bus_id` (unlike SPI's up-to-6
  `evt`-selected channels — `RCP_REGMAP_SIGNAL_I2C_SCL`/`_SDA` is itself
  only a single pair), so `evt` is always 0 and there is no channel
  selector or `BAD_CHANNEL` error on this endpoint type.
  `rcp_ep_i2c_encode_transfer_request()`/`_decode_transfer_request()`
  round-trip a transfer's raw outgoing bytes — target-device address
  byte(s) included, unparsed — as an `ACF_OP_WRITE` request, and
  `rcp_ep_i2c_encode_response()`/`_decode_response()` round-trip the raw
  captured bytes as an `ACF_OP_READ`-classified response (`ACF_ABB`
  untimed / `ACF_GBB` timed); decoded payloads are *borrowed* views into
  the caller's frame buffer, matching `ep_spi.h`'s own convention for the
  same raw-byte-stream/no-framing-help design — the explicit early
  validation of that design (ahead of LIN, milestone 71) the roadmap
  called for above.
- `rcp_ep_i2c_mode_t` names the four `i2c_mode` bus-speed presets; the
  file header documents, in this implementation's own words, the
  deliberate lower-numbered/conservative resolution chosen for the
  extraction §5.7/§7 high-speed-enum ambiguity, flagged pending spec
  errata rather than picked arbitrarily.
- UART models transmit and receive as two independent request/response
  families (`rcp_ep_uart_encode_write_request()`/`_decode_write_request()`
  + `_encode_write_response()`/`_decode_write_response()` for TX;
  `_encode_read_request()`/`_decode_read_request()` +
  `_encode_read_response()`/`_decode_read_response()` for RX) sharing one
  `rcp_ep_uart_functional_cfg_t` (baud rate, `uart_nr_bits`/parity/stop
  bits, `ep_rx_buffer_size`, `uart_timeout_ms`). A read request's
  `read_size` rides the existing ACF `byte_message_info` header's
  `read_size_or_segment_num` field rather than a new payload field; a
  read response may legitimately be shorter than the requested
  `read_size` (the `uart_timeout_ms` race), decoded exactly like any
  other payload length — this milestone's single-AVTPDU scope, with the
  `ms`/`segment_num`-driven multi-AVTPDU case explicitly deferred to
  Phase 20 (v0.76.0) and not pulled forward.
  `rcp_ep_uart_decode_read_request()` rejects any payload-bearing read
  request with the new `RCP_EP_UART_ERR_UNKNOWN_CMD`, documented in both
  the header and a decode-site comment as a deliberate asymmetry against
  GPIO's/the future PWM_OUT's request types, which do accept a payload on
  some of their own requests.
  `rcp_ep_uart_bit_pad_mask()`/`_apply_bit_padding()` are pure,
  directly-testable helpers for representing `uart_nr_bits < 8` words as
  one masked payload byte per word, this module's own wire-layout choice.
- Every functional-config mutator on both endpoint types
  (`rcp_ep_i2c_set_mode()`; `rcp_ep_uart_set_baud_rate()`/
  `_set_frame_format()`/`_set_rx_buffer_size()`/`_set_timeout()`) is
  gated by a thin, named wrapper over `rcp_server_field_writable()`
  (`RCP_SERVER_FIELD_FUNCTIONAL_W`) — reused, not duplicated, from
  `server.h` — matching `ep_spi.c`'s `rcp_ep_spi_functional_cfg_writable()`
  idiom exactly.
- `tests/test_ep_i2c.c` (19 cases) and `tests/test_ep_uart.c` (29 cases),
  and 44 new requirements (`REQ-I2C-001`..`016`, `REQ-UART-001`..`028`)
  added to `.fusa-reqs.json`; `cfusa trace --req-coverage 100` (both
  metrics), `cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify`
  (0 errors), and the full `ctest` suite (ASan/UBSan-clean) all stay
  green.

### 67. ADC + PWM_OUT + PWM_IN endpoints (v0.67.0) ✅

- `include/rcp/ep_adc.h`/`ep_pwm.h` + `.c`: ADC's three-layer averaging
  model (`adc_samples_per_avg_interval` → `adc_avg_intervals_per_request`
  → `adc_combine_avg_values`), request-driven sampling (no autonomous
  sampling — cyclic cadence via self-triggering or an external trigger
  source), `PWM_IN_NO_SIGNAL` on measurement timeout, and the
  first-sample-of-first-combined-value timestamp-capture-moment rule
  (extraction §5.9).
- PWM_OUT (period + active-duration 16-bit pair, same 8 write-semantics
  as GPIO, cycle-start/mid-pulse/done triggers useful for phase-locked
  ADC sampling) and PWM_IN (response-only capture of the same pair,
  rising/falling-edge triggers) — extraction §5.5–5.6.
- Compound-wait's numeric ≥/≤ comparison modes against PWM_IN's
  period/duty-cycle sub-fields (`evt[2:0]` = 100/101/110/111, extraction
  §4.6) implemented and unit-tested now, ahead of Phase 17's general
  compound-wait request-type landing, matching the SPI-truncation
  precedent set at v0.65.0.

**Done (v0.67.0)**: `include/rcp/ep_pwm.h` + `src/ep_pwm.c` (PWM_OUT +
PWM_IN + the PWM_IN compound-wait comparison helper) and
`include/rcp/ep_adc.h` + `src/ep_adc.c` land as new, additive
protocol-core surface, following `ep_gpio.h`/`ep_spi.h`/`ep_i2c.h`/
`ep_uart.h`'s (milestones 64–66) established layering discipline exactly:
ACF level only, with nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`,
`acf.h`/`acf.c`, `server.h`/`server.c`, `regmap.h`/`regmap.c`,
`discovery.h`/`discovery.c`, or any prior `ep_*` module touched.
`ep_adc.c`'s only new-module dependency is on `ep_pwm.h`, landing in this
same milestone alongside it, solely to reuse its `RCP_EP_PWM_IN_NO_SIGNAL`
sentinel rather than declare a second, inconsistent one.
- PWM_OUT and PWM_IN share one on-wire payload shape,
  `rcp_ep_pwm_value_t` (a big-endian `{period, active_duration}` 16-bit
  pair, `RCP_EP_PWM_PAYLOAD_LEN` = 4 octets) and address exactly one
  channel per `byte_bus_id` — no `evt`-selected channel, unlike
  `ep_spi.h`. `rcp_ep_pwm_out_apply_write()` reuses `ep_gpio.h`'s exact
  eight `evt[2:0]` write-semantics values as PWM_OUT's own
  `rcp_ep_pwm_out_write_semantics_t`, applying each of the six ordinary
  data-write operations independently to the period and active-duration
  fields (each its own 16-bit register, add/subtract saturating at
  0x0000/0xFFFF per field rather than wrapping or carrying between
  fields) — this module's own extension of GPIO's single-32-bit-register
  rule to a pair of independent 16-bit registers sharing one wire
  payload. `rcp_ep_pwm_out_apply_reconfig()` is this endpoint type's own
  single-flag analogue of GPIO's per-pin reconfiguration escape hatch
  (`evt[2:0]=7`): bit 0 of the reinterpreted write payload toggles a
  single `enabled` functional-config flag, adapted from GPIO's 32-pin
  bitmask selector down to PWM_OUT's own single output channel.
- `rcp_ep_pwm_out_trigger_fires()`/`rcp_ep_pwm_in_trigger_fires()`:
  cycle-start/mid-pulse/done for PWM_OUT and rising/falling for PWM_IN,
  the same pure trigger-evaluation pattern `ep_spi.h`/`ep_gpio.h` already
  established. PWM_IN has no write request of its own — only a read
  request (mirroring `ep_gpio.h`'s payload-free read request) answered
  by a response whose fields may legitimately equal
  `RCP_EP_PWM_IN_NO_SIGNAL` (`0xFFFF`, this module's own sentinel
  choice) when no valid edge-to-edge measurement completed in time.
- `rcp_ep_adc_average_interval()` (layer 1) computes one averaging
  interval's mean from caller-supplied raw samples, excluding any
  individual `RCP_EP_PWM_IN_NO_SIGNAL` sample from the mean and reporting
  `NO_SIGNAL` itself only when every sample in that interval timed out,
  with the interval's timestamp always its first sample's timestamp.
  `rcp_ep_adc_combine_avg_values()` (layers 2/3) combines
  `adc_avg_intervals_per_request` such per-interval results per
  `rcp_ep_adc_combine_mode_t` (average/min/max/latest — this module's own
  enumeration of plausible combine semantics, `LATEST` deliberately
  reporting its most recent interval's value verbatim, `NO_SIGNAL` or
  not). `rcp_ep_adc_capture_moment_timestamp()` is the single,
  directly-testable expression of the first-sample-of-first-
  combined-value rule: always `avg_values[0].timestamp`, independent of
  which combine mode selected the reported value itself. Neither
  function nor any other part of this milestone owns a timer, thread, or
  autonomous sampling loop — every raw sample is caller-supplied,
  matching the specification's request-driven sampling model referenced
  by name only.
- `rcp_ep_pwm_in_compound_wait_compare()`: the PWM_IN numeric ≥/≤
  comparison-mode helper this milestone's scope calls for, implemented
  and unit-tested now even though generic compound-wait request-type
  plumbing itself lands at Phase 17 milestone 69 — following the exact
  "isolated precedent" `ep_spi.h`'s
  `rcp_ep_spi_compound_wait_status_equal()` (milestone 65) set, per the
  roadmap's explicit instruction. Compares `captured.period` (`evt[2:0]`
  = 100/101) or `captured.active_duration` (`evt[2:0]` = 110/111, this
  module's own reading of "duty-cycle sub-field" as the raw
  active-duration tick count already reported, requiring no additional
  percentage computation) against a caller-supplied threshold, returning
  false — never a match — whenever the compared sub-field itself equals
  `RCP_EP_PWM_IN_NO_SIGNAL`.
- Every functional-config mutator on all three endpoint surfaces
  (`rcp_ep_pwm_out_set_trigger()`/`_set_enabled()`;
  `rcp_ep_pwm_in_set_trigger()`; `rcp_ep_adc_set_samples_per_avg_interval()`/
  `_set_avg_intervals_per_request()`/`_set_combine_mode()`) is gated by a
  thin, named wrapper over `rcp_server_field_writable()`
  (`RCP_SERVER_FIELD_FUNCTIONAL_W`) — reused, not duplicated, from
  `server.h` — matching every prior endpoint type's own idiom exactly.
- `tests/test_ep_pwm.c` (57 cases) and `tests/test_ep_adc.c` (34 cases,
  ASan/UBSan-clean), and 84 new requirements (`REQ-PWM-001`..`054`,
  `REQ-ADC-001`..`030`) added to `.fusa-reqs.json`; `cfusa trace
  --req-coverage 100` (both metrics), `cfusa check`/`lint`/`analyze`/
  `cyber`/`vuln`/`qualify` (0 errors), and the full `ctest` suite all
  stay green.

This closes out Phase 16 (Basic Endpoints) — every milestone from 64
through 67 is now landed.

---
### Phase 17 — Conditional Requests & Sequencers
---

Spec basis: extraction §2.7, §3.11, §3.14, §3.16. Flagged as likely
wholesale-new territory relative to the old informal protocol (extraction
§6 items 6–7) — if the old protocol only ever did simple immediate
read/write, this entire subsystem has no precedent in this codebase to
port from.

### 68. Compound + compound-wait requests + sequencers (v0.68.0) ✅

- `include/rcp/sequencer.h` + `src/sequencer.c`: persistent 8-bit
  sequencer-state registers (power-on default `1`, up to
  `svr_sequencers_max` exposed, `0` = compound operations unsupported
  entirely) — a first-class supporting primitive, not an implementation
  detail of compound requests.
- Compound (`request_type` `0x0F`/safety `0x8F`) and compound-wait
  (`0x0B`/`0x8B`): the `message_timestamp`-field-repurposing trick when
  `mtv=0` (first byte = `request_type` opcode, remaining 7 bytes = kind-
  specific sub-fields — state numbers, delays, repetition counts),
  `cmp_exec_delay`/`cmpw_exec_delay` timers, and the
  advance-sequencer-only-if-still-in-`cmp_start_state` guard (extraction
  §3.14).
- Per extraction §3.1's bundling rule, implemented as one all-or-nothing
  feature bundle: compound-wait support and **at least 4 sequencers** and
  the clear-non-safestate (`0x06`) cancellation type all ship together in
  this milestone, not compound-message-parsing alone — a repo claiming
  "compound support" without the other two per the spec's own bundle
  definition would be non-conformant.

**Done (v0.68.0)**: `include/rcp/sequencer.h` + `src/sequencer.c` land as
a standalone, dynamically-sized (`rcp_sequencer_table_new(count)`, heap-
allocated like `rcp.h`'s `rcp_bytes_t`, not a compile-time-capped array)
sequencer-state-register primitive, independently testable ahead of and
apart from compound.h's own use of it — exactly the "first-class
supporting primitive, not an implementation detail" split the roadmap
calls for. `include/rcp/compound.h` + `src/compound.c` land alongside it
as new, additive protocol-core surface, following `ep_gpio.h`/`ep_spi.h`/
.../`ep_adc.h`'s (milestones 64–67) established layering discipline:
nothing in `rcp.h`, `wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`,
`server.h`/`server.c`, `regmap.h`/`regmap.c`, or any `ep_*` module is
touched. Because `acf.c`'s own `rcp_acf_encode_gbb()` deliberately zeroes
the `message_timestamp` region whenever `mtv` is untimed (a milestone-60
rule predating this repurposing, left unchanged here per that same
layering discipline), `compound.c`'s own request encoders build the
ACF_GBB frame directly against `acf.h`'s already-published header layout
instead of calling into `acf.c`; decoding has no such conflict and goes
through `rcp_acf_decode_gbb()` unmodified.
`rcp_compound_step_t` is this module's own shared sub-field shape for
both compound and compound-wait (`sequencer_index`/`start_state`/
`next_state`/`exec_delay_ms`/`repeat_count`), the same one-payload-shape-
for-two-related-kinds precedent `ep_pwm.h`'s `rcp_ep_pwm_value_t` already
set. `rcp_compound_advance_guard()` is the pure, directly-testable
expression of the advance-only-if-still-in-`start_state` rule;
`rcp_compound_tick()` composes it with `rcp_compound_exec_delay_elapsed()`
for compound's own unconditional-after-the-delay timer, while
`rcp_compound_wait_tick()` composes the same guard with a
caller-supplied `condition_met` bool instead — this module owns no
endpoint-specific comparison logic of its own, consuming
`ep_spi.h`'s `rcp_ep_spi_compound_wait_status_equal()` and `ep_pwm.h`'s
`rcp_ep_pwm_in_compound_wait_compare()` exactly as the isolated
precedents milestones 65/67 built them to be. The clear-non-safestate
cancellation type (`request_type` `0x06`) ships in this same milestone via
`rcp_compound_encode_clear_non_safestate()`/`_decode_clear_non_safestate()`,
and `rcp_sequencer_table_new()` supports any count including the
roadmap's required **at least 4** — completing the all-or-nothing bundle
per extraction §3.1. `tests/test_sequencer.c` (12 cases) and
`tests/test_compound.c` (25 cases, ASan/UBSan-clean), and 35 new
requirements (`REQ-SEQ-001`..`011`, `REQ-CMP-001`..`024`) added to
`.fusa-reqs.json`; `cfusa trace --req-coverage 100` (both metrics) and
`cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify` (0 errors) stay
green, and the full `ctest` suite stays green.

### 69. Triggered/chained/timed requests + cancellation taxonomy (v0.69.0) ✅

- Triggered (`0x0E`/`0x8E`): trigger-occurrence counter reset on
  entering "started," counting independent of the endpoint's idle/busy
  state (only the final fire transition is idle-gated), `trigger_exec_delay`
  timer, infinite-repeat sentinel `0xFFFF`.
- Chained (`0x01`, no safety variant): forces sequential execution of 2+
  requests packed in one AVTPDU; `cs` bit semantics for chained requests
  (abort-on-predecessor-error vs. execute-regardless) and the
  `CHAIN_ABORTED`/`CHAIN_ERROR` error codes.
- Timed (`0x0A`, no safety variant): presentation-time-based execution as
  an alternative to a TSCF header — requires the time-sync bundle
  (`svr_implemented_options`) from Phase 13; `PRESENTATION_TIME_TOO_FAR`/
  `GPTP_FAIL` rejection paths.
- Cancellation taxonomy: clear-all (`0x05`, mandatory, already required
  since Phase 13's baseline) formalized alongside clear-non-safestate
  (`0x06`, shipped at v0.68.0) and clear-single (`0x07`, `clear_transaction_num`,
  `REQUEST_NOT_FOUND` on a miss) — general semantics (cancel between
  queued and executing only, chained-successor cancellation cascades,
  `REQUEST_CANCELED` per individually-cancelled request).
- **Execution priority ordering**, implemented as a genuine
  server-side scheduler property for the first time in this project's
  history (extraction §3.14): cancellation > triggered > timed > compound
  > compound-wait > chained > standard, ties FIFO. This is the direct
  spec-conformant replacement for the old `prioqueue.c`'s client-side
  priority heap — see Satellite Disposition.
- Multi-request-per-frame handling: independent per-ACF-message
  evaluation, one presentation time applying uniformly to every entry in
  a TSCF-headed frame (no mixed timed/untimed within one AVTPDU).

**Done (v0.69.0)**: five new peer modules land alongside compound.h/
compound.c (milestone 68), each following that module's exact
architectural template rather than depending on it directly — every
module (`triggered.h`/`triggered.c`, `chained.h`/`chained.c`, `timed.h`/
`timed.c`, `cancel.h`/`cancel.c`) that reuses the message_timestamp-
repurposing convention owns its own small pure helpers instead of
including compound.h, matching this project's established "one concept,
one module" layering discipline; nothing in `rcp.h`, `wire.c`, `avtp.h`/
`avtp.c`, `acf.h`/`acf.c`, `server.h`/`server.c`, `regmap.h`/`regmap.c`,
or any `ep_*` module is touched by any of them.
`triggered.h`/`triggered.c` implement the trigger-occurrence counter
(`rcp_triggered_runtime_t`, reset by `rcp_triggered_runtime_enter_started()`
and free-running via `rcp_triggered_runtime_record_occurrence()`
independent of endpoint idle/busy state) and `rcp_triggered_tick()`'s own
idle-gated fire transition, plus the wire-level `trigger_exec_delay_ms`
timer and the 16-bit `RCP_TRIGGERED_REPEAT_INFINITE` (`0xFFFF`) sentinel
(round-tripped only, mirroring compound's own `repeat_count` precedent).
`chained.h`/`chained.c` give acf.h's previously inert, round-tripped `cs`
field its first real behavior — `rcp_chained_advance()` is the pure,
per-member sequencing step implementing abort-on-error vs.
continue-regardless and reporting `RCP_CHAINED_MEMBER_CHAIN_ERROR`/
`_CHAIN_ABORTED` (this module's own spelling of `CHAIN_ERROR`/
`CHAIN_ABORTED`) per member. `timed.h`/`timed.c` implement
presentation-time admission via `rcp_timed_admit()`
(`RCP_TIMED_REJECT_GPTP_FAIL` takes priority over
`RCP_TIMED_REJECT_PRESENTATION_TIME_TOO_FAR`, wraparound-safe, a past
presentation_time is never "too far"), gated on
`rcp_timed_feature_enabled()` reading regmap.h's already-published
time-sync option bits. `cancel.h`/`cancel.c` complete the cancellation
taxonomy alongside milestone 68's clear-non-safestate: clear-all (`0x05`)
and clear-single (`0x07`, `REQUEST_NOT_FOUND` on a miss via
`rcp_cancel_attempt()`), plus the shared queued-vs-executing
cancellability window (`rcp_cancel_is_cancellable()`) and the
chained-successor cascade rule (`rcp_cancel_chain_should_cascade()`).
`scheduler.h`/`scheduler.c` are new protocol-core surface (not an
adaptation of `prioqueue.c`, which stays in the tree DEPRECATE-dispositioned
per the Satellite Disposition table until its own scheduled removal
milestone): `rcp_sched_classify()`/`rcp_sched_kind_rank()`/
`rcp_sched_compare()` implement the
cancellation > triggered > timed > compound > compound-wait > chained >
standard priority ordering with FIFO tie-breaking as a genuine total
order, and `rcp_sched_split_frame_members()`/
`rcp_sched_frame_timing_consistent()` implement multi-request-per-frame
handling (independent per-member evaluation, reading only acf.h's
already-published header layout; one presentation time applies uniformly
across every member of a TSCF-headed frame, mixed timed/untimed rejected).
`tests/test_triggered.c` (17 cases), `tests/test_chained.c` (11 cases),
`tests/test_timed.c` (12 cases), `tests/test_cancel.c` (12 cases), and
`tests/test_scheduler.c` (18 cases) — all five ASan/UBSan-clean — and 51
new requirements (`REQ-TRIG-001`..`013`, `REQ-CHAIN-001`..`010`,
`REQ-TIMED-001`..`008`, `REQ-CANCEL-001`..`012`, `REQ-SCHED-001`..`008`)
added to `.fusa-reqs.json`; `cfusa trace --req-coverage 100` (both
metrics) and `cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify` (0
errors) stay green, and the full `ctest` suite and `relay conform
--strict` both stay green.

---
### Phase 18 — E2E Protection (Safe Points)
---

Spec basis: extraction §4.7, §3.8. Flagged as "probably the most
safety-critical gap area" (extraction §6 item 4) — this is the direct,
explicit replacement for `e2e.c`'s ad-hoc CRC-16 mechanism named in this
program's own charter.

### 70. Safe points: CRC32 + safety-request variants (v0.70.0) ✅

- `include/rcp/safept.h` + `src/safept.c`: the exact CRC32 parameter set
  — polynomial `0xF4ACFB13`, width 32, init `0xFFFFFFFF`, final XOR
  `0xFFFFFFFF`, input/output reflection both true — replacing `e2e.c`'s
  CRC-16/CCITT-FALSE entirely, not alongside it.
- Coverage rule: CRC spans `stream_id` + `avtp_timestamp` (all-zero
  stand-in under NTSCF) + the full ACF header + payload, with the
  +1-quadlet/+4-octet length-accounting pre-adjustment for the trailing
  CRC bytes. `CRC_ERROR` on failure, execution skipped.
- Safety-request MSB-tagged variants (`0x8F`/`0x8B`/`0x8E` — compound/
  compound-wait/triggered only execute once the endpoint is in its
  configured safe state) and the **watchdog-purge-vs-safety-survive**
  rule: on watchdog overflow, normal (`0x0F`/`0x0B`/`0x0E`) requests are
  purged from the endpoint's queue while the safety variants remain
  active and become what actually drives the system to/through its safe
  state — implemented and tested as the primary safety mechanism it is,
  not an edge case.
- Per-stream `rx_enforce_e2e` (single-request drop vs. whole-stream
  latch-to-fault), `rx_wd_*` (enable/timeout/safestate-enable/info-enable),
  `rx_safety_measure` (0 = force-high-impedance vs. 1 = run a configured
  sequencer-based safety sequence), `rx_safestate_sequencer`/
  `rx_safe_sequencer_state` — wired against the register-map fields
  reserved (but inert) since Phase 14.
- **Fragmentation/CRC interaction rule modeled now, activated at Phase
  20**: only the last fragment of a multi-segment message carries a CRC
  (computed across the combined payload); the length-accounting
  adjustment applies only to that final segment.

**Done (v0.70.0)**: `include/rcp/safept.h` + `src/safept.c` land as new,
additive protocol-core surface, following every request-kind module's own
"own small pure helpers, don't reach into sibling modules" layering
discipline (the only dependency taken on is `sequencer.h`, milestone 68's
already-established shared primitive — the same one `compound.h`/
`triggered.h` both depend on directly). Nothing in `rcp.h`, `wire.c`,
`avtp.h`/`avtp.c`, `acf.h`/`acf.c`, `server.h`/`server.c`, or any `ep_*`
endpoint module is touched.
`rcp_safept_crc32()` implements the exact parameter set the roadmap calls
for via a reflected bit-at-a-time update (mirroring `e2e.c`'s own
`crc16_update()` structural precedent, not its CRC-16 table), verified
against the published CRC-32/AUTOSAR check value `0x1697D06A` over
`"123456789"` — an independent reference vector this parameter set
happens to coincide with. `rcp_safept_compute_crc()` is the coverage-span
wrapper (`stream_id` + `avtp_timestamp` + ACF header-and-payload, in
that order, with no intermediate allocation), `rcp_safept_length_with_crc()`
is the +1-quadlet/+4-octet length-accounting pre-adjustment, and
`rcp_safept_wrap()`/`rcp_safept_unwrap()` are the actual trailer
append/validate pair (`RCP_SAFEPT_ERR_CRC_MISMATCH` is this module's own
spelling of `CRC_ERROR`, with `*out_acf_frame` borrowed rather than
copied, matching `acf.c`'s own decode convention).
**Deviation from the bullet's "replacing `e2e.c`'s CRC-16/CCITT-FALSE
entirely, not alongside it" framing**: per this milestone's own explicit
scope (and mirroring milestone 69's identical `scheduler.c`/`prioqueue.c`
precedent), `safept.c` lands as `e2e.c`'s replacement in the sense that
matters now — new code adopts `safept.h`, not `e2e.h` — but `e2e.c`
itself stays REPLACE-dispositioned and physically in the tree, untouched,
pending its own scheduled removal at v0.79.0's satellite rework; this
milestone does not delete or modify `e2e.h`/`e2e.c`.
Safety-request MSB-tagged variants (`0x8F`/`0x8B`/`0x8E`) already existed
as round-tripped `compound.h`/`triggered.h` opcode constants since
milestones 68/69, both of which explicitly deferred gating their
execution to this milestone; `rcp_safept_is_safety_request()` (this
module's own MSB test) and `rcp_safept_request_may_execute()` are that
gate. The watchdog-purge-vs-safety-survive rule is
`rcp_safept_watchdog_purge_should_keep()`/`rcp_safept_watchdog_purge_classify()`
(operating on caller-owned `request_type` arrays, the same pattern
`scheduler.c`'s own `rcp_sched_compare()` established, rather than
reaching into `server.h`'s still request-kind-unaware
`rcp_server_endpoint_t` queue) plus `rcp_safept_wd_evaluate()`, which
ties `regmap.h`'s whole `rx_wd_*` family together into one pure
overflow/enter-safe-state/notify evaluation.
`regmap.h`'s `rcp_regmap_request_stream_cfg_t` is extended (not replaced)
with the fuller field set this milestone's own scope note flagged as
missing — `rx_wd_enable`, `rx_wd_safestate_enable`, `rx_wd_info_enable`,
`rx_safestate_sequencer`, `rx_safe_sequencer_state` — alongside the four
fields milestone 62 had already reserved (`rx_wd_timeout_ms`,
`rx_wd_action`, `rx_enforce_e2e`, `rx_safety_measure`), all now wired to
real behavior via `safept.h`'s own functions rather than the struct
itself growing behavior of its own.
`rcp_safept_endpoint_in_safe_state()` reads `rx_safety_measure` against a
live `sequencer.h` table (always true for
`RCP_SAFEPT_MEASURE_FORCE_HIGH_IMPEDANCE`; polls
`rx_safestate_sequencer`/`rx_safe_sequencer_state` for
`RCP_SAFEPT_MEASURE_SEQUENCER`) and fails *closed* — false, not true — on
an unrecognized measure byte or an invalid sequencer index, an explicit
engineering choice by this implementation documented in `safept.h`'s own
file header, not a value taken from the specification.
`rcp_safept_crc_error_action()` plus the caller-owned
`rcp_safept_stream_fault_t` latch (mirroring `e2e.h`'s own
`rcp_e2e_replay_guard_t` precedent for a small stateful helper type) wire
`rx_enforce_e2e`'s single-request-drop-vs-whole-stream-latch-to-fault
rule. `rcp_safept_fragment_carries_crc()` is the fragmentation/CRC
interaction rule modeled now (literally `is_last_fragment`, ready for
Phase 20 to call once real `segment_num`-driven reassembly exists) but
not otherwise activated.
`tests/test_safept.c` (35 cases, including a known-answer CRC32 vector
and an explicit end-to-end "watchdog overflow drives the full purge/
survive/safe-state-gate flow" scenario, verified ASan/UBSan-clean) and
`tests/test_regmap.c`'s existing zero-init test extended for the new
fields, plus 27 new requirements (`REQ-SAFEPT-001`..`027`) added to
`.fusa-reqs.json`; `cfusa trace --req-coverage 100` (both metrics) and
`cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify` (0 errors) stay
green, and the full `ctest` suite and `relay conform --strict` both stay
green.

---
### Phase 19 — Remaining Endpoint Types
---

Spec basis: extraction §5.10–5.13, §3.3–3.4. Each of LIN/CAN/ISELED/MDIO
is flagged as likely wholesale-new territory (extraction §6 items 9–11)
if the old informal protocol predates the newer entries (MDIO, CAN XL,
ISELED) or never modeled the others as raw-byte-pass-through in the first
place (LIN, Classical CAN). **DAC remains explicitly out of scope** per
the Phase 16 note — reserved `ep_type` code, no functional-config chapter,
revisit only on a future spec revision.

### 71. LIN commander endpoint (v0.71.0) ✅

- `include/rcp/ep_lin.h` + `src/ep_lin.c`: LIN bus commander (master)
  only, payload driven directly onto the bus, response generated when a
  received message's data matches the payload under the `evt[2:0]`
  comparison rule; transmission-done trigger, `lin_clk_divider`-derived
  bit-time clock.
- **Explicit scope validation, not assumed**: per extraction §5.10, no
  classic LIN-frame concept (checksum selection, PID/identifier
  generation, schedule tables) exists at this protocol layer — it's a
  "dumb" raw-byte pusher, with all LIN-frame semantics constructed
  client-side in the request payload. If any pre-replacement LIN handling
  in this repo's history assumed otherwise, that assumption does not
  carry forward; documented explicitly in `ep_lin.h`'s header comment so
  it isn't rediscovered the hard way later.

**Done (v0.71.0)**: `include/rcp/ep_lin.h` + `src/ep_lin.c` land as new,
additive protocol-core surface, following the same layering discipline
every endpoint type since milestone 64 established (nothing in `rcp.h`,
`wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`, `server.h`/`server.c`,
`regmap.h`/`regmap.c`, `discovery.h`/`discovery.c`, or any prior `ep_*`
module is touched). Modeled most closely on `ep_i2c.h`/`ep_uart.h`'s own
raw-byte-stream, controller-only shape (`ep_i2c.h`'s own file header had
already flagged this milestone as the next validation of that same
design), since LIN here is likewise commander-only with no protocol-level
framing help.
The scope validation the bullet above calls for is spelled out in
`ep_lin.h`'s own file header: no checksum-mode selection, no PID/
identifier generation, no schedule-table mechanism at this layer, and an
explicit note that the pre-replacement `linbr.c` LIN bridge stub's
frame-ID-shaped model (`rcp_lin_config_t.frame_id`) does not carry
forward to this endpoint type — `linbr.c` itself is untouched, its own
disposition (ADAPT, narrowed role) remaining Phase 21's job.
`rcp_ep_lin_compare_mode_t` (`rcp_ep_lin_compare_mode_valid()`/
`rcp_ep_lin_compare_fires()`) is this module's own original design for the
`evt[2:0]` comparison rule the bullet names but does not itself enumerate:
EXACT/PREFIX/ANY/NEVER plus four documented-no-op reserved values,
modeled on `ep_gpio.h`'s existing evt[2:0]-as-eight-value-selector
precedent (there, write semantics) and failing safe (never fires) for
NEVER and every reserved value, mirroring `ep_gpio.h`'s own RESERVED6
treatment. `rcp_ep_lin_trigger_t`/`rcp_ep_lin_trigger_fires()` is the
transmission-done trigger, narrowed from `ep_spi.h`'s three-trigger table
to the one event a commander-only LIN push actually produces.
`rcp_ep_lin_functional_cfg_t` composes `regmap.h`'s shared functional-cfg
prefix and adds `lin_clk_divider` (the bit-time clock divider) and
`trigger`, with `rcp_ep_lin_functional_cfg_writable()`/
`rcp_ep_lin_set_clk_divider()`/`rcp_ep_lin_set_trigger()` reusing
`server.h`'s `rcp_server_field_writable()` exactly as every prior
endpoint type's own setters do.
**Requirement-id naming note**: the pre-replacement `linbr.c` stub already
owns the `REQ-LIN-*` id prefix in `.fusa-reqs.json`, so this module's own
27 new requirements are tagged `REQ-LINEP-001`..`022` ("LIN endpoint")
instead — a collision-avoidance naming seam documented in `ep_lin.h`'s own
file header, not a numbering gap.
`tests/test_ep_lin.c` (22 cases covering the comparison-mode rules, the
trigger, functional-config authorization, and command-request/response
round trips) passes, alongside the full existing `ctest` suite (61/61).
`cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify` (0 errors) and
`cfusa trace --req-coverage 100` (both metrics) stay green, and
`relay conform --strict` passes against the rebuilt CLI.

### 72. CAN controller endpoint, incl. CAN XL (v0.72.0) ✅

- `include/rcp/ep_can.h` + `src/ep_can.c`: FrameFormat-selected Classical
  (CBFF/CEFF), FD (FBFF/FEFF), and XL (classical/new physical layer)
  frames, data frames only (no remote-frame support); CAN XL's 6 extra
  header bytes (RRS/SDT/VCID/AF) ahead of up to 2048 bytes of payload
  (2054B total) — the concrete driver for Phase 20's fragmentation
  go-decision.
- Separate bit-timing register sets for arbitration/FD-data/XL-data
  phases, delay-compensation-control, CAN-XL acceptance/ID filters, and
  the endpoint's own base-clock/divider registers scoped *only* to
  execution-delay timing (not bit-timing itself) — extraction §5.11.
- **No trigger-signal table populated**, matching the spec's own gap
  (extraction §7) — documented as an upstream spec gap in `ep_can.h`, not
  silently invented.
- **Terminology note carried from Phase 13**: this native CAN endpoint,
  CAN(FD/XL)-as-RCP's-own-transport (Phase 13), and the `canbr` bridge to
  an *external* CAN segment (Satellite Rework, v0.81.0) are three
  distinct things sharing one underlying bus technology — cross-referenced
  in each module's header comment to prevent the three-way duplication
  flagged in the Satellite Disposition table.

**Done (v0.72.0)**: `include/rcp/ep_can.h` + `src/ep_can.c` land as new,
additive protocol-core surface, following the same layering discipline
every endpoint type since milestone 64 established (nothing in `rcp.h`,
`wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`, `server.h`/`server.c`,
`regmap.h`/`regmap.c`, `discovery.h`/`discovery.c`, or any prior `ep_*`
module is touched). `rcp_ep_can_frame_format_t` selects among the six
FrameFormat variants the bullet above names (CBFF/CEFF, FBFF/FEFF,
XL_CLASSICAL_PL/XL_NEW_PL) via `evt[2:0]`, the same selector-via-evt
convention `ep_spi.h`'s channel selector already established, rejecting
the two undefined 3-bit codes with the new `RCP_EP_CAN_ERR_BAD_FRAME_FORMAT`
on decode (mirroring `ep_spi.h`'s own `BAD_CHANNEL`). Only data frames are
modeled — there is no remote-frame encode/decode path anywhere in this
module. `rcp_ep_can_xl_header_t` (sdt/vcid/af) carries CAN XL's extra
header content as this module's own 6-byte wire prefix (RRS is
deliberately not a separately-encoded field — its value is implied by
the frame-format selection itself; see `ep_can.h`'s own file header for
why). That file header also spells out, with actual numbers, why CAN
XL's worst-case frame is "the concrete driver for Phase 20's
fragmentation go-decision" the bullet above names: this module's own
worst-case encoded length (`RCP_EP_CAN_XL_MAX_ENCODED_LEN`, 2058 bytes)
exceeds `RCP_AVTP_NTSCF_MAX_PAYLOAD` (2047, `avtp.h`) — and NTSCF is the
only AVTPDU format an RC Server itself ever sends — so a worst-case CAN
XL response is explicitly single-AVTPDU/TSCF-only scope until Phase 20
lands, the same deliberate scope narrowing `ep_uart.h`'s own RX short-read
handling already carries forward from milestone 66.
Per extraction §5.11, `rcp_ep_can_functional_cfg_t` composes three
independent `rcp_ep_can_bit_timing_t` register sets (arbitration/FD-data/
XL-data), a delay-compensation control, a CAN-XL acceptance/ID filter
table (`rcp_ep_can_xl_filter_t`, `RCP_EP_CAN_XL_MAX_FILTERS` deep — this
module's own chosen depth, not a spec-derived number), and
`exec_delay_clk_divider`, this endpoint's own base-clock/divider register
scoped only to execution-delay timing, explicitly distinct from the three
bit-timing register sets. Every setter is gated by
`rcp_ep_can_functional_cfg_writable()`, a thin wrapper over `server.h`'s
`rcp_server_field_writable()`, reusing rather than duplicating that
authorization logic, matching every prior endpoint type's own setters.
Per extraction §7, this module defines **no** trigger-signal enumeration
at all — a documented reflection of a gap in the specification itself,
not an oversight, spelled out in `ep_can.h`'s file header and contrasted
explicitly against `ep_lin.h`'s TX_DONE trigger and `ep_gpio.h`'s per-pin
trigger table.
`ep_can.h`'s file header also carries forward the Phase 13 terminology
note: this native CAN endpoint, CAN(FD/XL)-as-RCP's-own-transport
(`avtp.h`, milestone 59 — cited there, not re-implemented here), and the
untouched `canbr.c` bridge stub to an *external* CAN segment (disposition
ADAPT/narrowed-role, Satellite Rework v0.81.0) are drawn out as three
distinct things sharing one bus technology, closing the loop the
Satellite Disposition table calls for without editing `avtp.h`/`canbr.h`
themselves (out of this milestone's layering-discipline scope).
**Requirement-id naming note**: the pre-replacement `canbr.c` stub already
owns the `REQ-CAN-*` id prefix in `.fusa-reqs.json`, so this module's own
22 new requirements are tagged `REQ-CANEP-001`..`022` ("CAN endpoint")
instead — the same collision-avoidance naming seam `ep_lin.h` established
for `REQ-LINEP-*` at v0.71.0.
`tests/test_ep_can.c` (41 cases covering frame-format/id-width/data-length
helpers, functional-config authorization across all three bit-timing sets
plus delay-compensation/exec-delay-divider/XL-filter setters, and
command-request/response round trips for both Classical and CAN XL
frames) passes, alongside the full existing `ctest` suite (62/62,
ASan/UBSan-clean). `cfusa check`/`lint`/`analyze`/`cyber`/`vuln`/`qualify`
(0 errors) and `cfusa trace --req-coverage 100` (both metrics) stay
green, and `relay conform --strict` passes against the rebuilt CLI.

### 73. ISELED endpoint (v0.73.0) ✅

- `include/rcp/ep_iseled.h` + `src/ep_iseled.c`: native 4-bit/5-bit ISELED
  framing over ISP_P/ISP_N, client payload taken as raw plain instruction/
  address/data content and ISELED-encoded by the endpoint; optional
  native ISELED CRC (separate from and additional to Phase 18's RCP-level
  CRC32); recovered-clock mode (`iseled_use_rcv_clk`) needing no ISP_N pin
  wired at all; single transmission-complete trigger.

**Requirement-id naming note**: unlike `ep_lin.h`'s `REQ-LINEP-*` and
`ep_can.h`'s `REQ-CANEP-*` (both collision-avoidance suffixes against a
pre-replacement bridge/stub module that already owned the unsuffixed
prefix), this codebase has never carried a pre-replacement ISELED
bridge/stub of any kind — verified directly against `.fusa-reqs.json`
before picking a prefix, so this module's own 24 new requirements are
tagged plain `REQ-ISELED-001`..`024`, no suffix needed.

**Done (v0.73.0)**: `include/rcp/ep_iseled.h` + `src/ep_iseled.c` land as
new, additive protocol-core surface, following the same layering
discipline every endpoint type since milestone 64 established (nothing in
`rcp.h`, `wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`, `server.h`/
`server.c`, `regmap.h`/`regmap.c`, `discovery.h`/`discovery.c`, or any
prior `ep_*` module is touched). The ACF-level command request/response
codec (`rcp_ep_iseled_encode_command_request()`/`_decode_command_request()`
and their response counterparts) carries the caller's plain instruction/
address/data content verbatim, unmodified — mirroring `ep_lin.h`'s own
raw-byte-stream convention — deliberately kept separate from this
endpoint's own native bit-framing engine
(`rcp_ep_iseled_symbol_encode()`/`_symbol_decode()`,
`rcp_ep_iseled_encode_bitframe()`/`_decode_bitframe()`), which is this
module's own original design: each 4-bit nibble frames onto ISP_P/ISP_N as
a 5-bit even-parity symbol, giving every symbol a built-in single-bit
integrity check and guaranteeing the transition density recovered-clock
mode needs (`0x0` and `0xF` nibbles are given different parity bits, so
they never repeat the same symbol back to back). `rcp_ep_iseled_crc8()`
implements a second, independent, optional integrity layer (a standard
CRC-8, poly 0x07/init 0x00 — a publicly documented algorithm, not
spec-derived, chosen the same way `e2e.c`'s own CRC-16/CCITT-FALSE already
is) gated by `iseled_crc_enable` and framed as a trailing content octet
*inside* the bit-framed symbol stream — explicitly independent of, and
stackable with, Phase 18's own RCP-level `e2e.c` wrap/unwrap, which
operates one layer further out and has no knowledge of ISELED at all; the
two integrity layers are never conflated. `iseled_use_rcv_clk` selects
recovered-clock mode, with `rcp_ep_iseled_requires_isp_n()` as a small,
pure, directly-testable statement that the ISP_N pin need not be
wired/mapped at all in that mode. `rcp_ep_iseled_trigger_t` names exactly
one real trigger (`TX_COMPLETE`) plus `NONE` — a single fixed trigger,
distinct from `ep_gpio.h`'s per-pin table and from `ep_can.h`'s documented
absence of any trigger table. `rcp_ep_iseled_functional_cfg_t` composes
`regmap.h`'s shared functional-cfg prefix and adds
`iseled_bit_clk_divider`, `iseled_use_rcv_clk`, `iseled_crc_enable`, and
`trigger`, with every setter gated by
`rcp_ep_iseled_functional_cfg_writable()`, a thin wrapper over
`server.h`'s `rcp_server_field_writable()`, reusing rather than
duplicating that authorization logic, matching every prior endpoint
type's own setters.
`tests/test_ep_iseled.c` (39 cases covering the bit-framing symbol
codec's round trips and corruption handling, the CRC-8 layer, the
recovered-clock helper, the transmission-complete trigger,
functional-config authorization, and command-request/response round
trips) passes, alongside the full existing `ctest` suite (63/63,
ASan/UBSan-clean). `cfusa lint`/`analyze`/`cyber`/`vuln`/`qualify` (0
errors) and `cfusa trace --req-coverage 100` (both metrics) stay green,
and `relay conform --strict` passes against the rebuilt CLI. `cfusa
check`'s own HARA002/HARA003 findings (10 hazards in `.fusa-hara.json`
each missing a risk rating and a `safetyGoals` reference) are a
pre-existing gap unrelated to this milestone — confirmed present, and
already failing that same CI job, on `main` at this milestone's own base
commit (`c27ab16`, before this branch's own changes) — not introduced or
touched here.

### 74. MDIO endpoint (v0.74.0) ✅

- `include/rcp/ep_mdio.h` + `src/ep_mdio.c`: the endpoint entirely absent
  from the spec's own informative "ten interfaces" scope list yet fully
  specified in its normative register map (extraction §1.2) — Clause-22
  MMD / Clause-45 MMS single-word and burst addressing modes, no
  type-specific functional config beyond the universal common block, no
  trigger-signal table. Reusable to expose an on-die integrated PHY's
  management registers when no physical MDIO pins are mapped at all.

**Requirement-id naming note**: verified directly against `.fusa-reqs.json`
before picking a prefix, the same check every prior endpoint milestone has
made — this codebase has never carried a pre-replacement MDIO bridge/stub
module, and none of this repository's satellite packages are named `mdio`,
so this module's own 19 new requirements are tagged plain
`REQ-MDIO-001`..`019`, no "-EP" collision-avoidance suffix needed.

**Done (v0.74.0)**: `include/rcp/ep_mdio.h` + `src/ep_mdio.c` land as new,
additive protocol-core surface, following the same layering discipline
every endpoint type since milestone 64 established (nothing in `rcp.h`,
`wire.c`, `avtp.h`/`avtp.c`, `acf.h`/`acf.c`, `server.h`/`server.c`,
`regmap.h`/`regmap.c`, `discovery.h`/`discovery.c`, or any prior `ep_*`
module is touched). `rcp_ep_mdio_addr_t` models both Clause-22 ("MMD", a
5-bit port/PHY address plus a 5-bit register address selecting one 16-bit
register directly) and Clause-45 ("MMS", the same 5-bit port/PHY address
joined by a 5-bit MMD device address and a full 16-bit register address),
with `rcp_ep_mdio_addr_valid()` as a small, pure, directly-testable
statement of both modes' field-range invariants at once. A request's
`word_count` selects single-word (1) vs. burst (>1) addressing;
`rcp_ep_mdio_burst_next_regad()` is this module's own pure,
directly-testable one-step-advance helper for a caller's own burst loop,
generalizing the well-known MDIO post-increment idiom to both addressing
modes uniformly, with wraparound at each mode's own register-address width
rather than an out-of-range result. `RCP_EP_MDIO_MAX_BURST_WORDS` (512) is
this module's own chosen cap, keeping every worst-case encoded frame
comfortably inside `RCP_AVTP_NTSCF_MAX_PAYLOAD` — the same deliberate
single-AVTPDU-worst-case scope `ep_uart.h`'s/`ep_lin.h`'s/`ep_iseled.h`'s
own request/response pairs already commit to, needing none of `ep_can.h`'s
own CAN-XL-specific exception. Following `ep_uart.h`'s own TX-write/RX-read
two-family precedent (rather than `ep_iseled.h`'s/`ep_can.h`'s single
request/response pair), this module exposes independent
read (`rcp_ep_mdio_encode_read_request()`/`_decode_read_request()`,
`rcp_ep_mdio_encode_read_response()`/`_decode_read_response()`) and write
(`rcp_ep_mdio_encode_write_request()`/`_decode_write_request()`,
`rcp_ep_mdio_encode_write_response()`/`_decode_write_response()`) request/
response families, because this endpoint type has two genuinely distinct
underlying MDIO operations, unlike a single symmetric command/reply
exchange. `rcp_ep_mdio_pack_words()`/`_word_count_of()`/`_unpack_word_at()`
(plus the pure `_word_encode()`/`_word_decode()` primitives) are this
module's own small, directly-testable big-endian register-word packing
layer, used by every encoder/decoder above but never touching the content
of any data word itself. Per the roadmap's own scope,
`rcp_ep_mdio_functional_cfg_t` composes only `regmap.h`'s shared
functional-cfg prefix and adds nothing of its own — a documented "nothing
more to add" finding, not an oversight, so there are no
`rcp_ep_mdio_set_*()` mutators in this file at all (no endpoint type in
this codebase exposes a setter for the common block's own fields either);
`rcp_ep_mdio_functional_cfg_init()` and
`rcp_ep_mdio_functional_cfg_writable()` are still provided purely for
consistency with every other endpoint type's own init/writable pair, the
latter a thin wrapper over `server.h`'s `rcp_server_field_writable()`.
Mirroring `ep_can.h`'s own documented reflection of the same spec gap
(extraction §7), this module defines no trigger enumeration and no
`rcp_ep_mdio_trigger_t`-shaped field anywhere. The file header also
documents this endpoint type's usefulness with zero physical MDIO/MDC pins
mapped at all, for exposing an on-die/integrated PHY's own management
registers entirely internally.
`tests/test_ep_mdio.c` (56 cases covering address validation for both
addressing modes, the burst-address-advance helper, the register-word
packing layer, functional-config authorization, and all four
read/write request/response round trips including short-frame/wrong-bus/
wrong-op/bad-address/bad-word-count rejection) passes, alongside the full
existing `ctest` suite (64/64, ASan/UBSan-clean). `cfusa
lint`/`analyze`/`cyber`/`vuln`/`qualify` (0 errors) and `cfusa trace
--req-coverage 100` (both metrics) stay green, and `relay conform --strict`
passes against the rebuilt CLI. `cfusa check`'s own HARA002/HARA003
findings (10 hazards in `.fusa-hara.json` each missing a risk rating and a
`safetyGoals` reference) are the same pre-existing gap ep_iseled.h's own
v0.73.0 entry above already documents — confirmed still present, and still
failing that same CI job, on `main` at this milestone's own base commit
(`61b912a`, before this branch's own changes; CI has in fact been red on
`main` since v0.72.0's own merge for this same reason) — not introduced or
touched here.

### 75. Wakeup control endpoint + power modes (v0.75.0) ✅

- `include/rcp/ep_wakeup.h` + `src/ep_wakeup.c`: the dedicated
  power-management endpoint (`ep_type=0x01`) — wake-source pin
  configuration/monitoring, the fixed `SleepCMD` (`0xA5`) request kind
  (distinct from the general request taxonomy), WakeUp-message emission
  in place of the generic trigger-signal mechanism.
- `include/rcp/power.h` + `src/power.c`: Normal/StandBy/Sleep/Unpowered
  modes (extraction §3.3–3.4) — StandBy reachable from Normal only via a
  hot start (no re-init), Sleep always a cold start (full re-init);
  hot-start-from-Sleep's four-step handshake (network-interface
  re-enable → repeating WakeUp message until echoed or repeat-limit hit →
  other response/ack queues resume → skip the handshake entirely for a
  network-level TC14/TC10 wake signal instead of the pin-wake path).
- Entry-refusal gating for both StandBy and Sleep requests (`wup_status`
  uncleared, a non-idle endpoint, or an unsent response/ack queue message
  ⇒ `REQUEST_CANCELED`), and the direct, spec-conformant replacement for
  `powerstate.c`'s ad-hoc Active/Sleeping/BusOff model (see Satellite
  Disposition — the client-side convenience wrapper moves to Satellite
  Rework v0.79.0, built on top of this milestone's actual protocol
  mechanism).

**Requirement-id naming note**: verified directly against `.fusa-reqs.json`
before picking prefixes for *both* new modules. A pre-existing
`REQ-PWR-001`..`010` group already exists (`src/powerstate.c`, the legacy
v0.13.0 client-side Active/Sleeping/BusOff satellite manager — untouched by
this milestone; its own REPLACE work is Satellite Rework v0.79.0), so
`power.h`/`power.c` deliberately use the distinct prefix `REQ-PWRMODE-*`
(13 requirements) rather than colliding with or renumbering that
pre-existing group. `ep_wakeup.h`/`ep_wakeup.c` carry no pre-existing
`REQ-WAKEUP-*` group of any kind, so they use that prefix plain, 16
requirements, no collision-avoidance suffix needed — the same check every
prior endpoint milestone has made.

**Done (v0.75.0)**: `include/rcp/power.h` + `src/power.c` land as new,
additive protocol-core surface, cross-referencing `lifecycle.h` (not
`server.h`) for lifecycle-state concerns, per the module boundary the
#87/#88 naming split established. `rcp_pwrmode_t` names the four modes
under its own `rcp_pwrmode_`/`RCP_PWRMODE_` prefix, deliberately distinct
from `powerstate.h`'s own `rcp_power_state_t`/`RCP_POWER_*` names to avoid
any symbol collision and to keep this module visibly separate from the
legacy client-side model it supersedes at the wire level.
`rcp_pwrmode_transition()` implements the general Normal/StandBy/Sleep/
Unpowered transition table (Normal↔StandBy hot; Normal/StandBy→Sleep,
any→Unpowered, and Unpowered→Normal cold; anything else, including
Sleep→Normal, rejected — the latter routed instead through its own
function). `rcp_pwrmode_wake_from_sleep()` classifies waking from Sleep:
always hot for a network-level wake path (`RCP_PWRMODE_WAKE_VIA_NETWORK`,
the roadmap's TC14/TC10 signal), and for a pin-level wake
(`RCP_PWRMODE_WAKE_VIA_PIN`) hot only once the caller has driven
`rcp_pwrmode_handshake_t` through all four handshake steps
(`_iface_reenabled()` → repeated `_wakeup_attempt()` until echoed or its
own repeat-limit is hit → `_resume_queues()`), cold otherwise — this
module's own explicit, directly-testable state machine for the roadmap's
four-step sequence, rather than documented prose. `rcp_pwrmode_check_entry()`
is the shared StandBy/Sleep entry-refusal gate, taking plain bools
(`wup_status_clear`, `endpoint_idle`, `response_queue_empty`) rather than
reaching into any endpoint module directly, keeping this module
endpoint-agnostic; a `NULL` gate is treated as refused, the same
fail-safe convention `lifecycle.h`'s own NULL-snapshot handling already
established.

`include/rcp/ep_wakeup.h` + `src/ep_wakeup.c` land alongside it as this
endpoint type's own wire encoding (`ep_type=0x01`). SleepCMD gets its own
dedicated, non-taxonomy wire encoding — an ACF_ABB message whose payload
is the fixed `RCP_EP_WAKEUP_SLEEPCMD_OPCODE` (`0xA5`) marker byte followed
by the requested `rcp_pwrmode_t` target (`RCP_PWRMODE_STANDBY` or
`RCP_PWRMODE_SLEEP` only; anything else rejected with
`RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE`), entirely outside the message_timestamp-
repurposing convention `request_compound.h`/`request_triggered.h`/
`request_chained.h`/`request_timed.h`/`request_cancel.h` share — with a
response carrying `power.h`'s own `rcp_pwrmode_entry_result_t` outcome.
`rcp_ep_wakeup_encode_wakeup_message()` is this endpoint's own dedicated
emission path (its own fixed `RCP_EP_WAKEUP_WAKEUP_OPCODE` marker,
distinct from SleepCMD's own byte) replacing the generic trigger-signal
mechanism every other endpoint type uses — there is no
`rcp_ep_wakeup_trigger_t` anywhere in this file.
`rcp_ep_wakeup_is_wakeup_echo()` is the small predicate feeding
`power.h`'s handshake step (b) its own `echoed` argument.
`rcp_ep_wakeup_functional_cfg_t` composes `regmap.h`'s shared prefix and
adds its own `sources[RCP_EP_WAKEUP_MAX_SOURCES]` (8) wake-source table
(`enabled` + `active_high` polarity per slot), with
`rcp_ep_wakeup_source_asserted()`/`_any_source_asserted()` as small, pure,
directly-testable statements of which sources currently indicate a wake
condition given caller-sampled raw pin levels; `rcp_ep_wakeup_wup_status_t`
is this module's own minimal `wup_status` latch
(`_latch()`/`_clear()`/`_is_clear()`), which a caller wires into `power.h`'s
`rcp_pwrmode_entry_gate_t` — the two modules are deliberately not
compile-coupled the other way (`power.h` does not include `ep_wakeup.h`).

`tests/test_power.c` (32 cases covering the general transition table, the
hot/cold wake-from-Sleep classification for both wake paths, the
handshake state machine including out-of-order-call rejection and
repeat-limit exhaustion, and the entry-refusal gate) and
`tests/test_ep_wakeup.c` (28 cases covering wake-source polarity/
monitoring, the wup_status latch, SleepCMD request/response round trips
and their short-frame/wrong-bus/wrong-message-type/bad-opcode/bad-target-
mode rejection, the WakeUp message round trip, and echo recognition) both
pass, alongside the full existing `ctest` suite (66/66, ASan/UBSan-clean,
verified locally). `cfusa lint`/`analyze`/`cyber`/`vuln`/`qualify`,
`cfusa trace --req-coverage 100`, and `relay conform --strict` were not
re-run locally in this pass (no local `cfusa`/`relay` toolchain available
in this environment) — left for this PR's own CI run to confirm, as
every prior milestone's own CI run already does independently of this
note. `cfusa check`'s own HARA002/HARA003 findings (10 hazards in
`.fusa-hara.json` each missing a risk rating and a `safetyGoals`
reference) are the same pre-existing gap ep_mdio.h's own v0.74.0 entry
above already documents — confirmed still present in `.fusa-hara.json` at
this milestone's own base commit (`b85c351`, before this branch's own
changes; CI has been red on `main` since v0.72.0's own merge for this
same reason) — not introduced or touched here.

---
### Phase 20 — Fragmentation
---

Spec basis: extraction §2.5, §4.7, §7. **Explicit go/no-go call, not left
open**: **GO.** Three features already committed earlier in this same
plan — CAN XL's up to 2054-byte frames (v0.72.0), UART's FIFO-vs-timeout
short reads (v0.66.0), and full-register-map discovery reads on
larger-endpoint-count servers (v0.63.0) — functionally need multi-AVTPDU
support; skipping it would mean either capping CAN XL's usable payload far
below what v0.72.0 itself claims to support, or refusing full-register
discovery past whatever fits in one frame. Since it's one generic
mechanism (`ms` bit + `segment_num`) reusable across every endpoint that
needs it, implementing it once here is cheaper than three separate ad hoc
size-capping workarounds bolted onto three different endpoint modules.

### 76. Fragmentation support (v0.76.0) ✅

- Multi-AVTPDU reassembly for both ACF_ABB and ACF_GBB: `ms=1`
  intermediate fragments carrying `segment_num` in place of `read_size`,
  the final `ms=0` fragment carrying the true `acf_msg_length`/payload-
  completion semantics and (per Phase 18) the only fragment allowed to
  carry a safe-point CRC, computed across the full reassembled payload.
- `rx_stream_max_request_size` (0 = fragmentation unsupported for that
  stream) wired into the request-stream config table reserved since
  Phase 14; per-endpoint retrofit of CAN XL (v0.72.0), UART (v0.66.0),
  and discovery (v0.63.0) to actually exercise it end-to-end, closing out
  the three deferred single-AVTPDU-worst-case tests noted at those
  milestones.

**Module-naming note**: per RELAY spec v1.14 §13.7.2's registry (checked
before picking a name, per the pattern v0.75.0's own "Requirement-id
naming note" set), multi-frame message fragmentation/reassembly is a
constrained name — `fragment` — so this milestone lands as new
`include/rcp/fragment.h` + `src/fragment.c`, not a `frag.h`/folded-into-
`acf.h` alternative.

**Done (v0.76.0)**: `include/rcp/fragment.h` + `src/fragment.c` land as
new, additive protocol-core surface layered on top of `acf.h`/`acf.c`
(milestone 60), interpreting the `ms` bit and dual-purpose
`read_size_or_segment_num` field that module has round-tripped, unused,
since its own original milestone. This module owns the mechanism exactly
once, generically — `rcp_fragment_plan_count()`/`_plan()` (encode side)
split a payload into a caller-owned array of `rcp_fragment_segment_t`
(offset/len/ms/segment_num), reducing to a single unfragmented `ms=0`
segment whenever the payload already fits, so fragmentation is a strict
superset of every prior milestone's own single-frame wire format, not a
parallel one; `rcp_fragment_reassembler_t` (decode side) is a small,
caller-owned accumulator (mirroring `e2e.h`'s own `rcp_e2e_stream_fault_t`
precedent) enforcing strictly-monotonic, zero-based `segment_num`
ordering and a caller-supplied `max_total_len` ceiling, failing closed
(`RCP_FRAGMENT_REASM_ERR_TOO_LARGE`/`_ERR_OUT_OF_ORDER`) rather than
growing unbounded or accepting a corrupt sequence. Neither half calls into
`acf.c`, `e2e.h`, or any endpoint module directly — the same "own small
pure helpers, operate on caller-owned data" layering discipline `e2e.h`
and `scheduler.h` already established; `e2e.h`'s own
`rcp_e2e_fragment_carries_crc()` hook (modeled since milestone 70, ready
for this milestone to call) is unchanged and is what a caller drives
against `fragment.h`'s own final-segment determination to decide which
one frame of a sequence gets a safe-point CRC.

`regmap.h`'s `rcp_regmap_request_stream_cfg_t` gains
`rx_stream_max_request_size` (a `size_t`, zero-initialized by the
existing `rcp_regmap_request_stream_cfg_init()` — no new requirement-id
group needed, the same precedent Phase 18's own E2E/watchdog field
additions to this struct set): 0 means fragmentation is unsupported for
that stream, matching this milestone's own roadmap wording; a nonzero
value is the ceiling a caller passes as `fragment.h`'s own
`max_fragment_payload` (encode) or `max_total_len` (decode).

Per-endpoint retrofit, all three targets named above:
`rcp_ep_can_encode_frame_response_fragmented()`/
`_decode_frame_response_fragment()`/`_decode_reassembled_frame_response()`
(`ep_can.h`/`ep_can.c`) close CAN XL's own deferred worst-case test with a
*genuine* driver — a full 2048-byte CAN XL captured frame's combined
prefix-then-data ACF payload (2058 octets) does not fit in a single
`RCP_AVTP_NTSCF_MAX_PAYLOAD` (2047)-byte NTSCF AVTPDU, exactly the gap
this phase's own go-decision named; the new test suite round-trips that
exact worst case through real fragmentation and reassembly. The matching
`rcp_ep_uart_encode_read_response_fragmented()`/
`_decode_read_response_fragment()` (`ep_uart.h`/`ep_uart.c`) and
`rcp_discovery_encode_response_fragmented()`/`_decode_response_fragment()`/
`_decode_reassembled_response()` (`discovery.h`/`discovery.c`) retrofits
are documented honestly rather than oversold: both endpoints' own
`read_size` field is one octet wide (max 255), so neither one's genuine
wire traffic can ever actually produce a payload needing more than one
fragment — their own test suites instead exercise the mechanism
end-to-end against a deliberately small `max_fragment_payload`, which
still closes out the single-AVTPDU-worst-case tests those two milestones
(66, 63) left open by proving the retrofit composes correctly against
each endpoint's own wire codec, not by fabricating a real-world scenario
that field width does not actually allow.

`tests/test_fragment.c` (27 cases: strerror/result-string coverage,
`plan_count()`/`plan()` boundary and layout cases including the exact
256-intermediate-segment ceiling, the full reassembler state machine
including out-of-order rejection and the `max_total_len` fail-closed
path, reset/reuse across messages, and one test composing a plan directly
against `rcp_e2e_fragment_carries_crc()`) and the new fragmentation cases
added to `tests/test_ep_can.c` (7), `tests/test_ep_uart.c` (4), and
`tests/test_discovery.c` (4) all pass, alongside the full existing
`ctest` suite (67/67, ASan/UBSan-clean, verified locally). `cfusa
lint`/`analyze`/`cyber`/`vuln`/`qualify`, `cfusa trace --req-coverage
100`, and `relay conform --strict` were not re-run locally in this pass
(no local `cfusa`/`relay` toolchain available in this environment) — left
for this PR's own CI run to confirm, the same note every prior milestone
in this environment has made. `cfusa check`'s own HARA002/HARA003
findings (the same pre-existing `.fusa-hara.json` gap documented at every
milestone since v0.72.0's own merge) are unchanged and not introduced or
touched here.

---
### Phase 21 — Satellite Package Rework
---

Works back through every entry in the Satellite Package Disposition table
above, now that Phases 13–20 have landed a stable new core API to rebind
against. Grouped into batches the same way the legacy program's own
"coverage maximization" milestones (v0.50.0–v0.54.0) were batched, for the
same reason: each batch is independently shippable and CI-green on its
own, rather than one enormous PR.

### 77. Foundational test/config satellites (v0.77.0) ✅

- **REPLACE** `mock.c`: in-process RC-Server/endpoint test double
  replacing the old zone-controller mock, built against the Phase 14
  register-map model and Phase 16+ endpoint types directly (no
  Command/Response shape left to double for).
- **REPLACE** `config.c`: RC-Server/endpoint manifest loader (HW pin map,
  EP list, stream config) replacing the old zone-manifest JSON schema.
- **ADAPT** `cli.c`: `capabilities`'s payload content moves from the old
  `RCP_CMD_*` enum to the new `svr_implemented_options` feature-bundle
  flags; `version`/`status` structurally unaffected.

**Legacy-double relocation note**: `mock.c`'s replacement drops the
`rcp_controller_t`/`rcp_registry_t` vtable pair entirely, but 18 not-yet-
migrated legacy satellites' own unit tests (`authz`, `ratelimit`, `loan`,
`observe`, `faultinject`, `admin`, `recorder`, `deadline`, `watchdog`,
`powerstate`, `tsn`, `proxy`, `redundancy`, `federation`, `zonegroup`,
`prioqueue`, `firmware`, `adapt`, plus `bench_mock`/`command_latency_test`)
still build fixtures against exactly that legacy double — those satellites
are Satellite Rework's own later batches (v0.79.0-v0.84.0), out of this
milestone's scope. Rather than block this REPLACE on migrating 18 unrelated
modules early, the old `include/rcp/mock.h`/`src/mock.c` content moved
verbatim (same struct layout, same function/type names, same
`REQ-CTRL-*`/`REQ-REG-*`/`REQ-RESP-*`/`REQ-STAT-*`/`REQ-ERR-011` coverage)
to `tests/legacy_mock.h`/`tests/legacy_mock.c` — a test-only translation
unit, no longer shipped as part of the public `rcp` library, linked
directly into each of those 18 test binaries (`tests/CMakeLists.txt`).
`tests/test_mock.c` was renamed to `tests/test_legacy_mock.c` unchanged
(new `rcp_legacy_mock` ctest entry) and a from-scratch `tests/test_mock.c`
now tests the new module instead. This keeps the full test suite green on
its own, per this Phase's own "each batch is independently shippable"
framing, without pre-empting v0.79.0-v0.84.0's own migration work.

**Done (v0.77.0)**: `include/rcp/mock.h` + `src/mock.c` land as a
TC18-shaped `rcp_mock_server_t`: an `rcp_lifecycle_state_t`, an
`rcp_regmap_general_t` (mutable, directly exposed via
`rcp_mock_server_regmap()`), and a fixed `RCP_MOCK_MAX_ENDPOINTS`-slot
table pairing `server.h`'s own `rcp_server_endpoint_t` ep_enable/queue with
a caller-supplied `rcp_mock_endpoint_handler_fn` — this module owns no
per-endpoint wire semantics itself, never calling into any `ep_*.c`
directly, matching `lifecycle.h`/`regmap.h`'s own "operate on caller-owned
data" layering. `rcp_mock_server_dispatch()` runs one already-framed
request through `lifecycle.h`'s `rcp_lifecycle_should_accept()` first (a
rejected frame never reaches an endpoint, mirroring a real server), then
the addressed endpoint's `rcp_server_endpoint_submit()` decides whether its
handler runs immediately or the request queues; `rcp_mock_server_drain_endpoint()`
is the queue's other half. New `REQ-MOCK-001`..`018` (18 requirements,
verified against `.fusa-reqs.json` before picking the prefix — no
pre-existing `REQ-MOCK-*` group).

`include/rcp/config.h` + `src/config.c` land as a from-scratch manifest
loader: an optional `server` object (`vendor_id`/`device_id`/`magic`/
`svr_implemented_options`, the last a named-group array mapped onto
`regmap.h`'s `RCP_REGMAP_OPT_*` bits) plus `hw_pin_map`/`endpoints`/
`streams` entry lists, parsed by the same hand-rolled, non-nesting-aware
JSON scanner style the old `config.c` used (documented as such, including
the scanner's own known looseness). `rcp_config_apply_to_mock()` sets the
new `mock.c` double's regmap fields and registers one endpoint per manifest
entry; `hw_pin_map`/`streams` are parsed data only (regmap.h's own
`hw_pin_map`/`request_stream_cfg` fields are location/capacity descriptors,
not per-entry backing storage yet, so there is nothing to apply them into).
`REQ-CFG-001`..`006` were rewritten in place (new schema, same six-entry
budget) and `REQ-CFG-007`..`010` added for `parse_json`'s own
whole-document behavior and the two `apply_to_mock`/`load` entry points.

`src/cli.c`'s `capabilities_json()` now derives its `"features"` array from
`RCP_REGMAP_OPT_*` group membership (`time_sync`/`enhanced_cancel`/
`compound_bundles`) instead of the old, legacy-satellite-flavored
`["loaning"]`; `version_json()`/`status_json()` untouched, per scope.
`tests/test_mock.c` (24 cases) and `tests/test_config.c` (14 cases) are
both from-scratch rewrites; the full `ctest` suite (68/68, including the
new `rcp_legacy_mock` target) passes, verified locally under both a plain
Debug build and a manual `-fsanitize=address,undefined` build (sequential
`ctest -j1`; ASan+UBSan-clean — a `-j8` parallel run produced spurious
"Subprocess aborted" failures that did not reproduce sequentially or when
each binary was run standalone, an environment artifact of this sandbox,
not a defect). `cfusa lint`/`analyze`/`cyber`/`vuln`/`qualify`, `cfusa
trace --req-coverage 100`, and `relay conform --strict` were not re-run
locally (no local `cfusa`/`relay` toolchain in this environment, the same
note every prior milestone has made) — `REQ-MOCK-*`/`REQ-CFG-*`
`//cfusa:req`/`//cfusa:test` tag sets were cross-checked 1:1 by hand before
pushing. `cfusa check`'s own pre-existing HARA002/HARA003 findings are
unchanged and not introduced or touched here.

### 78. Transport satellites (v0.78.0) ✅

- **REPLACE** `udp.c`: becomes the IEEE1722-over-UDP/IP (Annex J)
  transport carrying AVTPDUs; POSIX socket/thread plumbing from the old
  implementation reused as a starting point where it still fits.
- **REPLACE** `shmem.c`: in-process AVTPDU-frame loopback transport;
  evaluate consolidating with the new `mock.c` rather than shipping both.
- **ADAPT** `tsn.c`: 802.1p PCP tagging retained, priority source moved
  from the retired `rcp_priority_t` enum to the new request-kind
  execution-priority classes (Phase 17, extraction §3.14).
- **DEPRECATE** `tls.c`: removed; the spec's real security control is
  MACsec (802.1AE, link-layer), out of this library's transport
  abstraction entirely (declared product-specific/opaque in the register
  map, same as PHY/network-interface config).

**Done (v0.78.0)**: `include/rcp/udp.h` + `src/udp.c` are a from-scratch
`rcp_avtp_transport_t` (avtp.h, milestone 59) implementation --
`rcp_udp_avtp_transport_dial()` connects a UDP socket to a fixed peer;
`rcp_udp_avtp_transport_bind()` binds a local association and learns its
peer address from the first datagram it receives (a deliberate,
documented single-peer-per-socket simplification, not per-stream_id
multi-peer routing -- see the header's own file comment). The old
`rcp_udp_controller_t`/`rcp_udp_zone_server_t` pair's pending-request
rendezvous-by-id, Subscribe/Unsubscribe control frames, and Zone-addressed
Command/Response/Status codec are gone outright: none of that has a role
left once the transport's own job shrinks to "move an already-framed
AVTPDU," per the REPLACE disposition's own reasoning. What *is* reused
from the predecessor module, per this milestone's own text: the POSIX
socket create/bind/connect/getsockname helpers. `recv()` polls the
socket in short slices rather than blocking directly on it, so `close()`
-- called from a different thread than whichever one is inside `recv()`
-- reliably unblocks that call on every targeted platform without relying
on non-portable shutdown-interrupts-a-blocking-recv semantics; `close()`
itself only raises a flag, deferring the real `close(fd)` to `destroy()`,
so a racing `close()` can never invalidate the fd out from under a
`select()`/`recvfrom()` call already in flight. Windows keeps the
predecessor module's own documented stub (`ok()` always false,
`send()`/`recv()` always `RCP_ERR_CLOSED`) rather than gaining a new
winsock implementation. New `REQ-UDP-001`..`014` (rewritten in place,
same prefix/count band as the predecessor's own 19, reduced because the
correlation/subscription machinery those old requirements described no
longer exists in this module).

`include/rcp/shmem.h` + `src/shmem.c` land as
`rcp_shmem_avtp_pair_new()`: two cross-wired `rcp_avtp_transport_t`
sides sharing one pair of bounded FIFOs, so whatever one side sends the
other side's `recv()` receives, entirely in-process. This is
deliberately not a thin rename of `avtp.c`'s own
`rcp_avtp_loopback_transport_new()` (a single self-echoing instance,
which cannot play two distinct in-process parties at once) and
deliberately not folded into `mock.c` either (which doubles a server's
*dispatch* logic against already-decoded parameters and never moves raw
bytes) -- `shmem.h`'s own file header documents this three-way
distinction (echo one node's own traffic / connect two distinct
in-process nodes / double a server's dispatch behavior) in response to
this milestone's own "evaluate consolidating" instruction, per the same
practice `canbr.h`/`linbr.h` used for their own three-way native-endpoint/
transport-network/bridge distinction. New `REQ-SHMEM-001`..`009`
(rewritten in place).

`include/rcp/tsn.h` + `src/tsn.c` keep the SO_PRIORITY/802.1p PCP-tagging
mechanism unchanged and wrap an `rcp_avtp_transport_t` instead of the
retired `rcp_controller_t`. `rcp_tsn_classify_frame()` peeks the outgoing
AVTPDU's subtype and, for a repurposed ACF_GBB message, its
`request_type` opcode (reusing `acf.h`/`request_compound.h`'s own already-
published decode helpers, adding no new wire-parsing logic of its own) to
derive an `rcp_sched_kind_t` (scheduler.h, milestone 69), which
`rcp_tsn_pcp_for()` maps to a PCP value via a map indexed directly by
`rcp_sched_kind_t` (default: `pcp[kind] = rcp_sched_kind_rank(kind)`).
`recv()`/`close()` are pure passthroughs, matching the predecessor
module's own scope (PCP tagging is egress-only). New `REQ-TSN-001`..`007`
(rewritten in place).

`include/rcp/tls.h`, `src/tls.c`, and `tests/test_tls.c` are removed
outright, and `REQ-TLS-001`..`013` removed from `.fusa-reqs.json` with
them -- the spec's actual security control is MACsec (802.1AE,
link-layer), which the Satellite Disposition table above describes as
declared product-specific/opaque register-map configuration, not
something this library's transport abstraction should keep wrapping.
`authz.h`'s and `rcp.h`'s own doc comments, and `PORTABILITY.md`'s module
list, had their `tls.h`/`tls.c` references updated to stop pointing at a
now-deleted file; `CYBERSECURITY.md`/`tara.md`'s own TLS-layer framing is
left untouched, per the Satellite Disposition table's own "Requirements/
safety/security artifacts... REPLACE, deliberately last" entry -- re-
deriving that certification evidence is Phase 22's job (v0.85.0), not
this milestone's.

`tests/test_udp.c` (10 cases, including a real cross-thread
close()-unblocks-a-blocked-recv() test), `tests/test_shmem.c` (11 cases),
and `tests/test_tsn.c` (10 cases) are all from-scratch rewrites; the full
`ctest` suite (67/67, one fewer than v0.77.0's 68 now that
`rcp_tls` is gone) passes, verified locally under both a plain Debug
build and a manual `-fsanitize=address,undefined` build (ASan+UBSan-clean
across the full suite, run sequentially). `cfusa lint`/`analyze`/`cyber`/
`vuln`/`qualify`, `cfusa trace --req-coverage 100`, and `relay conform
--strict` were not re-run locally (no local `cfusa`/`relay` toolchain in
this environment, the same note every prior milestone has made) --
`REQ-UDP-*`/`REQ-SHMEM-*`/`REQ-TSN-*` `//cfusa:req`/`//cfusa:test` tag
sets were cross-checked 1:1 by hand before pushing, and `REQ-TLS-*`
confirmed to have zero remaining `//cfusa:req`/`//cfusa:test` references
anywhere in the tree before removing it from `.fusa-reqs.json`. `cfusa
check`'s own pre-existing HARA002/HARA003 findings are unchanged and not
introduced or touched here.

### 79. Safety-adjacent satellites (v0.79.0) ✅

- **REPLACE** `watchdog.c`: thin client convenience around the Phase 18
  per-stream `rx_wd_*` registers and safety-request sequences, not an
  independent `RCP_CMD_WATCHDOG` side channel.
- **REPLACE** `deadline.c`: liveness re-derived from response/ack-queue
  `Flush_time` heartbeats and/or `rx_wd_info_enable` notifications — no
  generic "Status stream" left to monitor.
- **REPLACE** `powerstate.c`: client convenience wrapper around the
  Phase 19 Wakeup endpoint + Normal/StandBy/Sleep/Unpowered model,
  replacing the ad-hoc Active/Sleeping/BusOff state machine entirely.

**Done (v0.79.0)**: `include/rcp/watchdog.h` + `src/watchdog.c` land as
`rcp_watchdog_keeper_t`, rebuilt around e2e.h's pure `rcp_e2e_wd_evaluate()`
(milestone 70) instead of the retired `RCP_CMD_WATCHDOG` command: a Keeper
now tracks, per registered *request stream* (not zone), a copy of that
stream's `rx_wd_enable`/`rx_wd_timeout_ms`/`rx_wd_safestate_enable`/
`rx_wd_info_enable` configuration (regmap.h) plus a last-kick timestamp,
and a background thread periodically re-runs `rcp_e2e_wd_evaluate()`
against the elapsed time since that kick, firing a subscribed callback
whenever the resulting `rcp_e2e_wd_result_t` (`overflowed`/
`enter_safe_state`/`notify`) changes. `rcp_watchdog_keeper_kick()` is the
new module's own spelling of "a safety-request sequence for this stream
just completed" — this module sends no wire traffic and owns no
transport of its own, matching e2e.h's own "operate on caller-owned
data" layering. The old `rcp_health_state_t` Healthy/Degraded/Faulted
enum is dropped outright (not re-derived): `rcp_e2e_wd_result_t`'s own
three independent booleans are already the TC18-shaped verdict, and
layering a parallel severity ladder on top would only duplicate what
`rx_wd_safestate_enable`/`rx_wd_info_enable` already express. `REQ-WDG-*`
rewritten in place (same 9-id band, same prefix — no collision, this
module owns that prefix already).

`include/rcp/deadline.h` + `src/deadline.c` land as
`rcp_deadline_monitor_t`, re-keyed on *request stream* and driven by two
caller-pushed signals instead of a subscribed Status stream (which no
longer exists in the TC18 model): `rcp_deadline_monitor_heartbeat()` — a
caller calls this on every observed response/ack-queue flush
(`rcp_regmap_response_queue_cfg_t::flush_time_us`, regmap.h) — resets a
stream's deadline timer and reports it alive again if it was dead;
`rcp_deadline_monitor_notify_overflow()` — a caller calls this on an
observed `rx_wd_info_enable` watchdog-overflow notification (e2e.h's
`rcp_e2e_wd_result_t.notify`, e.g. relayed from this same milestone's own
`rcp_watchdog_keeper_t`) — declares the stream dead immediately, without
waiting out its deadline window. A background thread still evaluates
each stream's own deadline timer against the current time and declares
it dead once too much time has passed without a heartbeat, preserving
the module's original "silence means dead" contract with a pushed signal
in place of a subscribed one. `REQ-DL-*` rewritten in place (same 8-id
band).

`include/rcp/powerstate.h` + `src/powerstate.c` land as
`rcp_powerstate_manager_t`, a client-side convenience wrapper over
milestone 75's own already-shipped protocol-core mechanism (power.h's
`rcp_pwrmode_t`/`rcp_pwrmode_transition()`/`rcp_pwrmode_wake_from_sleep()`/
`rcp_pwrmode_handshake_t` and ep_wakeup.h's SleepCMD/WakeUp wire codec),
replacing the ad-hoc Active/Sleeping/BusOff enum and
`RCP_CMD_SLEEP`/`RCP_CMD_WAKE` calls entirely — there is no BusOff-
equivalent fault state left to recover from in the TC18 model, so this
module needs no background retry thread at all (dropped outright, along
with `close()` — every action here is now caller-driven). Per
`rcp_avtp_addr_t`-addressed endpoint (replacing `rcp_zone_t`), the
Manager tracks a mirrored `rcp_pwrmode_t` and (for the pin-wake hot-start
path) an `rcp_pwrmode_handshake_t`, and supplies paired encode/apply
functions a caller drives around its own choice of transport — this
module sends no bytes and owns no transport itself, matching
udp.c/shmem.c/tsn.c's own "the transport is a distinct concern"
precedent from v0.78.0:
`rcp_powerstate_manager_encode_entry_request()`/`_apply_entry_response()`
drive a client-initiated StandBy/Sleep request against ep_wakeup.h's
SleepCMD codec, applying `rcp_pwrmode_transition()` once a matching
response arrives (transaction-number-correlated, rejecting a
stale/mismatched response rather than acting on it);
`rcp_powerstate_manager_wake_via_network()` drives the always-hot
network-level wake path directly; `_handshake_begin()`/
`_encode_wakeup_probe()`/`_apply_wakeup_echo()`/
`_handshake_resume_queues()`/`_wake_via_pin()` are thin, endpoint-scoped
pass-throughs over power.h's own handshake step functions and
ep_wakeup.h's WakeUp codec. Per power.h's own file header note, the
pre-existing `REQ-PWR-001`..`010` group belongs to this module and is
rewritten in place (same prefix, same 10-id band, verified against
`REQ-PWRMODE-*`/`REQ-WAKEUP-*` for zero collision before reusing it) —
no renumbering needed, since those two prefixes were already chosen
distinctly at milestone 75 for exactly this reason.

`tests/test_watchdog.c` (11 cases), `tests/test_deadline.c` (9 cases),
and `tests/test_powerstate.c` (22 cases) are all from-scratch rewrites,
none of them linking `tests/legacy_mock.c` any more (all three moved off
`rcp_controller_t`/`rcp_zone_t` entirely — `tests/CMakeLists.txt`,
`tests/legacy_mock.h`, and `include/rcp/mock.h`'s own file-header
satellite lists updated to match); the full `ctest` suite (67/67,
unchanged from v0.78.0 — this milestone neither adds nor removes a test
binary) passes, verified locally under both a plain Debug build and a
manual `-fsanitize=address,undefined` build (ASan+UBSan-clean, run
sequentially). `cfusa lint`/`analyze`/`cyber`/`vuln`/`qualify`, `cfusa
trace --req-coverage 100`, and `relay conform --strict` were not re-run
locally (no local `cfusa`/`relay` toolchain in this environment, the
same note every prior milestone has made) — `REQ-WDG-*`/`REQ-DL-*`/
`REQ-PWR-*` `//cfusa:req`/`//cfusa:test` tag sets were cross-checked 1:1
by hand before pushing. `cfusa check`'s own pre-existing HARA002/HARA003
findings are unchanged and not introduced or touched here.

**PR #97 follow-up (landing fix)**: CI's `cfusa check`/`cfusa lint`
jobs came back red on the branch above with a single `CFUSA-L004`
(MISRA-C 2012 Rule 17.2, no-recursion) error against `src/watchdog.c`'s
static `evaluate()`, even though that function is not recursive — its
only call at the flagged line is to e2e.h's unrelated
`rcp_e2e_wd_evaluate()` (milestone 70). This is a new instance of the
same lint-rule bug class as `SoundMatt/c-FuSa#59` (the false-positive
that motivated retiring the CI escape hatch at milestone 49): the
checker appears to key recursion detection off of a name-suffix match
rather than an actual call graph, and a local function named `evaluate`
calling anything ending in `..._evaluate` collides with it. Fixed
locally, in this repo only, by renaming the static function (and its
sole call site in `evaluate_all()`) to `evaluate_stream` — a pure
rename with no behavior change, so no `REQ-WDG-*` tag or test needed
updating. Verified with a real local `cmake`/`ctest` build (67/67
passing), a from-source local build of `cfusa` v0.5.46 (`cfusa lint`
and `cfusa check` both exit 0 with zero `CFUSA-L004` findings post-
rename; `cfusa trace` reports 929/929 requirements traced), and a local
build of RELAY's own `relay conform --strict` against the CLI target
(PASS). A narrowly-scoped issue describing this new false-positive
pattern was filed against `SoundMatt/c-FuSa` (issue only, that repo's
own source is untouched, per this ecosystem's cross-repo policy) so the
upstream recursion check can move to real call-graph resolution instead
of name matching; no broad CI waiver was reinstated for it.

**Deferred, not forgotten**: `FORMAL_VERIFICATION.md`'s `HealthStateMachine.tla`/
`WatchdogProtocol.tla` mapping-to-C-implementation table and
`PORTABILITY.md`'s thread-per-decorator inventory both still describe
(or, for `PORTABILITY.md`, have been updated to reflect) the shapes this
milestone's rewrite touches; per the Satellite Disposition table's own
"Requirements/safety/security artifacts... REPLACE, deliberately last"
entry, re-deriving `FORMAL_VERIFICATION.md`'s own TLA+-to-C mapping table
(and TARA/CYBERSECURITY.md's still-stale `REQ-E2E-*` references, a gap
e2e.h's own milestone-70 REPLACE already flagged) is Phase 22's job
(v0.85.0), not this milestone's — `PORTABILITY.md` itself *was* updated
here since it made a factual claim (`powerstate.c` spawns a background
thread) this milestone's rewrite made false.

### 80. Generic decorators, batch 1 (v0.80.0) ✅

- **ADAPT**, rebound to the new endpoint-request/response primitive in
  place of `rcp_controller_t`'s `send(cmd)`: `authz.c` (policy keys move
  to stream/endpoint/request-type), `ratelimit.c` (admission control
  against an endpoint's finite request-queue capacity), `loan.c`
  (zero-copy pooled buffers for CAN XL/UART/SPI-sized payloads),
  `observe.c` (metric labels move to server/endpoint/request-type),
  `faultinject.c` (drop/slow/error/timeout rules), `admin.c` (lists RC
  Servers/endpoints instead of zones), `recorder.c` (captures raw
  ACF messages/AVTPDUs instead of Command/Response pairs, with a new
  on-the-wire capture format).

**Done (v0.80.0)**: Unlike milestone 79's three modules, none of these
seven had a single generic `rcp_controller_t` choke point left to
re-anchor to a new equivalent -- Phase 16-19 built 13 heterogeneous,
independently-typed endpoint modules instead of one. Every module here
therefore drops its old vtable-wrapper shape entirely and becomes a
plain, caller-driven library operating on caller-owned/already-classified
data, the same "sends no wire traffic and owns no transport" pattern
milestone 79's watchdog.c/deadline.c/powerstate.c established -- a
caller now calls the appropriate function directly, immediately before
or after whichever endpoint-specific (`ep_gpio.h`-shaped) encode/apply
call it's actually making, instead of interposing on a `send()` that no
longer exists.

`authz.h`/`authz.c`: `rcp_authz_policy_t` keeps its shape (identity ->
permitted-address/request-type entries) but is rekeyed from
`rcp_zone_t`/`rcp_command_type_t` to `avtp.h`'s `rcp_avtp_addr_t`
(stream_id + byte_bus_id) plus a caller-supplied `request_type` byte --
left deliberately opaque to this module (it may be `acf.h`'s
`rcp_acf_op_t` for a Standard request or a peer request-kind module's own
opcode, e.g. `request_compound.h`'s 0x0F/0x8F), matching every
request-kind module's own "operate on caller-classified data" layering.
The old `AuthController` wrapper and `identity_fn` indirection are
dropped outright: `rcp_authz_policy_permit()` is now the whole
interception point, called directly by the caller.

`ratelimit.h`/`ratelimit.c`: `rcp_ratelimit_limiter_t` now keeps one
independent token bucket per `rcp_avtp_addr_t`, lazily created on first
use, rather than one shared bucket for a whole wrapped controller --
matching the roadmap's own "an endpoint's own finite capacity" framing.
The old `exempt_critical` flag (`RCP_PRIORITY_CRITICAL`, a retired
client-assigned tag) is replaced by `exempt_safety`, keyed on `e2e.h`'s
`rcp_e2e_is_safety_request()` MSB test against the caller-supplied
`request_type` -- the one client-visible request-kind signal that
actually survives the protocol replacement.

`loan.h`/`loan.c`: the free-list pool itself is unchanged (still a real
free-list, not cpp-RCP's write-only one); only the `LoaningController`
wrapper is gone. `rcp_loan_pool_acquire()`/`rcp_loan_return()`/
`rcp_loan_release()` replace `rcp_controller_loan()`/
`rcp_controller_send_loaned()` -- there is no `send_loaned()`
counterpart any more, since a caller now drives whichever endpoint's own
request-sending path directly with the loaned buffer.

`observe.h`/`observe.c`: `rcp_span_t`/`rcp_metric_t` move their
`zone`/`cmd_type` fields to `addr`/`request_type`; counter names move
from `rcp.commands.*` to `rcp.requests.*`. `rcp_observe_record()`
replaces the `ObservingController` wrapper: the caller measures its own
start/end `rcp_monotonic_ms()` timestamps around whichever request it
just drove and passes them in directly.

`faultinject.h`/`faultinject.c`: the rule list/count-expiry logic is
byte-for-byte the same engine as before (this module was never
zone/command-addressed to begin with); only the interception point
changes. `rcp_fi_action_t` (`PROCEED`/`DROP`/`SLOW`/`ERROR`/`TIMEOUT`)
replaces the old implicit "return an `rcp_errc_t`/mutate a
`rcp_response_t`" contract, and `rcp_faultinject_evaluate()` -- a public
version of the old private `fi_pick()` -- replaces the
`FaultInjectingController` wrapper.

`admin.h`/`admin.c`: the SSE-style subscribe/emit channel and the
Prometheus counter/metrics-text renderer are untouched (they never read
a zone out of `rcp_command_t` to begin with). Only the listing surface
changes: `rcp_admin_server_new()` no longer wraps an `rcp_registry_t` (no
TC18 counterpart exists); `rcp_admin_server_register_endpoint()`/
`_deregister_endpoint()`/`_endpoints()` replace
`rcp_admin_server_zones()`, with whatever application code discovers RC
Servers (`discovery.h`, a manifest, etc.) telling this module directly.

`recorder.h`/`recorder.c`: `rcp_recorder_entry_t` now holds a raw
captured frame (`rcp_bytes_t`) tagged with an `rcp_avtp_addr_t` and an
`inbound` flag, instead of a `rcp_command_t`/`rcp_response_t` pair -- a
new on-the-wire capture format, per this milestone's own roadmap scope,
with its own from-scratch binary layout (timestamp/stream_id/byte_bus_id/
inbound/frame_len/frame). `rcp_recorder_capture()` replaces the
`RecordingController` wrapper; `rcp_playback_run_all()` now drives a
caller-supplied `rcp_playback_deliver_fn` callback instead of a single
generic target controller, leaving "what replaying a frame means" to the
caller.

`tests/test_authz.c`, `test_ratelimit.c`, `test_loan.c`, `test_observe.c`,
`test_faultinject.c`, `test_admin.c`, and `test_recorder.c` are all
from-scratch rewrites (7, 7, 6, 8, 10, 7, and 11 cases respectively),
none of them linking `tests/legacy_mock.c` any more (all seven moved off
`rcp_controller_t`/
`rcp_zone_t`/`rcp_registry_t` entirely -- `tests/CMakeLists.txt`,
`tests/legacy_mock.h`, and `include/rcp/mock.h`'s own file-header
satellite lists updated to match); the full `ctest` suite (67/67,
unchanged from v0.79.0 -- this milestone neither adds nor removes a test
binary) passes, verified locally under both a plain Debug build and a
manual `-fsanitize=address,undefined` build (ASan+UBSan-clean across the
full suite, run sequentially; macOS ASan does not support leak detection,
so leak-checking itself is not covered by this local run). Unlike every
prior milestone's note, a local `cfusa` v0.5.46 toolchain build and a
local `relay conform --strict` build *were* available this time: `cfusa
lint`/`check`/`analyze`/`cyber`/`qualify`/`vuln` all exit 0 against a
clean (no stray `build*/` directories) tree; `cfusa trace
--req-coverage 100` reports 929/929 requirements traced (100%, both
metrics) -- the tool's own advisory `UNTRACED` list still names four
pre-existing gaps this milestone does not touch (`REQ-MDNS-007/008`,
`REQ-PQ-005`, `REQ-RELAY-013`), unchanged from before this PR; and a
local build of RELAY's own `relay conform --strict` against the CLI
target reports PASS. `REQ-AUTH-*`/`REQ-RL-*`/`REQ-LOAN-*`/`REQ-OBS-*`/
`REQ-FI-*`/`REQ-ADMIN-*`/`REQ-REC-*` are rewritten in place in
`.fusa-reqs.json` (same seven prefixes, same id-band counts: 8/9/8/13/
10/8/11).

**Deferred, not forgotten**: `CYBERSECURITY.md` and `HARA.md` both still
name pre-rebind symbols this milestone renamed or removed
(`rcp_authz_controller_new()`, `rcp_ratelimit_controller_t` /
`exempt_critical`, `rcp_faultinject_controller_t`, `rcp_prioqueue_controller_t`
-- the last belongs to milestone 83's still-pending DEPRECATE, not this
one). Per the Satellite Disposition table's own "Requirements/safety/
security artifacts... REPLACE, deliberately last" entry, re-deriving
those two documents' own prose (and TARA/CYBERSECURITY.md's separately
already-flagged stale `REQ-E2E-*` references, milestone 70's own gap) is
Phase 22's job (v0.85.0), not this milestone's.

### 81. Protocol bridges (v0.81.0) ✅

- **ADAPT** all seven compile-time-stub bridges (`grpcbridge`,
  `restbridge`, `someipbr`, `ddsbr`, `mqttbr`, `udsbr`, `doipbr`):
  framing calls updated to the new endpoint-request/response shape; none
  currently link a real backend, so none require one now either.
- **ADAPT**, narrowed role: `canbr.c`/`linbr.c` — CAN and LIN are now
  native endpoint types (Phase 19) and CAN(FD/XL) can be RCP's own
  transport network (Phase 13); these two bridges keep only the
  external-foreign-bus-segment gateway role that doesn't overlap either.
  Documented cross-references added in all three modules' header
  comments (native endpoint, transport-network option, bridge) so the
  three-way distinction survives future contributors, per the Satellite
  Disposition table.

**Done (v0.81.0)**: All nine bridge stubs (the seven general-purpose
ones plus `canbr.c`/`linbr.c`) were still built against the pre-Phase-13
`rcp_controller_t`/`rcp_zone_t` vtable shape milestones 77-80 already
rebound everything else off of -- the only satellites left on it. Every
module drops its `rcp_<x>_controller_t` wrapper and
`rcp_<x>_controller_new(rcp_zone_t, cfg)` entry point entirely and
replaces it with a single plain function, `rcp_<x>_bridge_send(cfg, addr,
request_type, payload, payload_len, out_response)`, operating on
`avtp.h`'s `rcp_avtp_addr_t` plus a caller-supplied, deliberately opaque
`request_type` byte in place of `rcp_zone_t`/`rcp_command_type_t` --
exactly the pattern milestone 80 established, applied here to modules
that carry no real backend rather than to modules with actual internal
state. Every config struct (`rcp_grpc_config_t` and its eight siblings)
and its `_default_config()` keep their pre-existing fields and default
values unchanged; only the call shape wrapped around them changes. All
nine still return `RCP_ERR_NOT_SUPPORTED` unconditionally (no backend
newly linked in this milestone) and now additionally document, in the
same breath, that `*out_response` is left untouched on that path.

`canbr.h`/`linbr.h` each gained a new "Narrowed role" section in their
own file header spelling out the three-way (CAN) / two-way (LIN)
distinction the Satellite Disposition table calls for: `ep_can.h`/
`ep_lin.h` (native endpoint, Phase 19), `avtp.h`'s
`rcp_avtp_transport_t` (CAN(FD/XL)-as-transport, Phase 13, CAN only --
LIN has no transport role), and this bridge's own remaining
external-foreign-segment-gateway role. `ep_can.h` (milestone 72) and
`ep_lin.h` (milestone 71) already carried their own half of this same
distinction, explicitly deferring the `canbr.h`/`linbr.h`-side write-up
to "Phase 21... tracked for Satellite Rework v0.81.0" -- this milestone
closes that loop from the `canbr.h`/`linbr.h` side without re-touching
`ep_can.h`/`ep_lin.h`/`avtp.h` themselves, per those modules' own stated
layering discipline ("avtp.h and canbr.h themselves are not touched
here").

`tests/test_grpcbridge.c` and its eight siblings are from-scratch
rewrites (2 cases each: `bridge_send()` returns `RCP_ERR_NOT_SUPPORTED`
and leaves `*out_response` untouched; `_default_config()` returns its
documented defaults), none of them ever having linked
`tests/legacy_mock.c` to begin with. `REQ-GRPC-*`/`REQ-REST-*`/
`REQ-SOMEIP-*`/`REQ-DDS-*`/`REQ-MQTT-*`/`REQ-UDS-*`/`REQ-DOIP-*`/
`REQ-CAN-*`/`REQ-LIN-*` are rewritten in place in `.fusa-reqs.json`
(same nine prefixes, reduced from 4 to 2 requirements each -- the
retired `zone()`/`subscribe()`/`close()` vtable slots no longer exist to
have their own requirements). `REQ-CANEP-*`/`REQ-LINEP-*` (`ep_can.c`/
`ep_lin.c`'s own, already-disjoint id bands) are untouched.

Verified locally: full `ctest` suite (67/67, unchanged from v0.80.0)
under both a plain Debug build and a manual
`-fsanitize=address,undefined` build (ASan+UBSan-clean across the full
suite; macOS ASan does not support leak detection, so leak-checking
itself is not covered by this local run). A local `cfusa` v0.5.46
toolchain build: `lint`/`check`/`cyber`/`qualify`/`vuln` all exit 0
against a clean tree; `trace --req-coverage 100` reports 911/911
requirements traced (100%, both metrics) -- the tool's own advisory
`UNTRACED` list still names the same four pre-existing gaps milestone 80
already flagged (`REQ-MDNS-007/008`, `REQ-PQ-005`, `REQ-RELAY-013`),
unchanged by this milestone. A local build of RELAY's own `relay conform
--strict` against the `-DRELAY_BUILD_CLI=ON` CLI target reports PASS.

### 82. Optional discovery convenience (v0.82.0) ✅

- **ADAPT** `mdns.c`: retained strictly as an optional convenience
  discovery layer scoped to the IEEE1722-over-UDP/IP transport variant,
  layered beside — never instead of — the Phase 15 native broadcast
  discovery mechanism, which remains the only mandatory discovery path.

**Done (v0.82.0)**: `mdns.h`/`mdns.c` rebound off the retired `rcp_zone_t`/
`rcp_zone_string()` onto `avtp.h`'s `rcp_stream_id_t` -- the same server
identity `discovery.h`'s `rcp_discovery_result_t.server_stream_id` already
uses (milestone 63) -- so an mDNS-advertised record and a
natively-discovered one describe the same kind of thing. `rcp_mdns_zone_info_t`
becomes `rcp_mdns_server_info_t` (`server_stream_id`/`host`/`port`/
`instance_name`); the discoverer/announcer vtables and the `StaticDiscoverer`
test double are otherwise structurally unchanged, only rekeyed.
`rcp_mdns_make_instance_name()` drops the zone-per-service `"<zone>.<host>.
_rcp._udp.local"` scheme (no zone concept survives to name a record after)
in favor of `"<server_stream_id as 16 hex digits>.<host>._rcp-tc18._udp.local"`
-- a service type distinct from any legacy naming, scoped to this module's
own IEEE1722-over-UDP/IP convenience role rather than a value the
specification itself defines.

`mdns.h`'s file header gained an explicit "Optional, and never a substitute
for discovery.h" section, the same cross-reference discipline milestone 81
used for `canbr.h`/`linbr.h`'s three-way CAN/LIN distinction: this module
is scoped strictly to the IEEE1722-over-UDP/IP transport variant, sits
beside `discovery.h`'s mandatory native broadcast discovery (Phase 15,
milestone 63) rather than replacing it, and neither module depends on the
other.

`tests/test_mdns.c` is a from-scratch rewrite of the same eight cases
(StaticDiscoverer emits/stops/carries-fields, instance-name generation,
Announcer register/withdraw/destroy) against `rcp_stream_id_t`-keyed
records instead of zones. `REQ-MDNS-001..009` are rewritten in place in
`.fusa-reqs.json` (same prefix and count -- no vtable slots were added or
removed, only rekeyed, unlike the bridges' milestone-81 shrink).

Verified locally: full `ctest` suite (67/67, unchanged from v0.81.0) under
both a plain Debug build and a manual `-fsanitize=address,undefined` build
(ASan+UBSan-clean; macOS ASan has no leak detector, so leak-checking is not
covered by this local run). A local `cfusa` v0.5.46 toolchain build:
`lint`/`check`/`cyber`/`qualify`/`vuln` all exit 0 against a clean tree;
`trace --req-coverage 100` reports 911/911 requirements traced (100%, both
metrics) -- the tool's own advisory `UNTRACED` list still names the same
four pre-existing gaps prior milestones already flagged (`REQ-MDNS-007/008`,
`REQ-PQ-005`, `REQ-RELAY-013`), unchanged by this milestone since no vtable
slot count changed here. A local build of RELAY's own `relay conform
--strict` against the `-DRELAY_BUILD_CLI=ON` CLI target reports PASS.

### 83. Deprecation batch (v0.83.0) ✅

- **DEPRECATE** (removed from the tree, with rationale preserved in this
  ROADMAP.md and this milestone's commit message, not silently deleted):
  `prioqueue.c` (superseded by protocol-defined execution-priority
  ordering, Phase 17), `firmware.c` (no OTA/firmware endpoint exists
  anywhere in the spec — an OEM/application-layer concern out of RCP's
  own scope), `zonegroup.c`/`proxy.c`/`redundancy.c`/`federation.c` (all
  built entirely on the retired Zone concept, with no TC18 counterpart
  for multi-target grouping, proxying, server redundancy, or multi-HPC
  leasing), `dyndata.c` (every endpoint type has one fixed, spec-defined
  payload shape — no schema-negotiation problem exists to solve).

**Done (v0.83.0)**: the seven satellites the Satellite Disposition table
already called **DEPRECATE** are gone from the tree outright, executing
those pre-agreed dispositions rather than re-litigating them:
`prioqueue.h`/`.c`, `firmware.h`/`.c`, `zonegroup.h`/`.c`, `proxy.h`/`.c`,
`redundancy.h`/`.c`, `federation.h`/`.c`, and `dyndata.h`/`.c`, plus each
one's `tests/test_*.c`. None of the seven had a live caller anywhere else
in `src/`/`include/` (verified by grep before deletion), so removal is a
pure subtraction:

- `prioqueue.c` — TC18 makes execution priority a server-side property of
  request *kind* (cancellation > triggered > timed > compound >
  compound-wait > chained > standard, Phase 17), not a value a client
  attaches before sending. A client-side priority heap has nothing left
  to order.
- `firmware.c` — no OTA/firmware-update endpoint or message exists
  anywhere in the spec; it is explicitly an OEM/application-layer concern
  the protocol itself has no hook for.
- `zonegroup.c`, `proxy.c`, `redundancy.c`, `federation.c` — all four are
  built on the retired Zone concept (grouping, proxying, and the
  Controller/Zone pair specifically). TC18 addresses
  server/endpoint/`(stream_id, byte_bus_id)` instead, has no multi-target
  broadcast-grouping concept, delegates multi-hop/bridging to "the
  network" rather than the RC system, models an RC Server as a single
  node with one lifecycle state (no server-redundancy concept), and has
  no counterpart to the old zone/HPC-lease federation registry.
- `dyndata.c` — every TC18 endpoint type has one fixed, spec-defined
  payload shape, so there is no schema-negotiation problem left for a
  client-side dynamic-data registry to solve.

`CMakeLists.txt`'s source list and `tests/CMakeLists.txt`'s matching
`add_executable`/`target_link_libraries`/`add_test` blocks drop all seven
modules; the `tests/CMakeLists.txt` comment above `test_ratelimit`
explaining which suites still link `legacy_mock.c` is reworded so it no
longer refers to these six as "not-yet-migrated" (they were never
migrated — they were removed). `.fusa-reqs.json` drops the now-orphaned
`REQ-PQ-*` (9), `REQ-FW-*` (9), `REQ-ZG-*` (6), `REQ-PROXY-*` (8),
`REQ-RED-*` (9), `REQ-FED-*` (10), and `REQ-DYN-*` (6) requirement
entries — 57 requirements total, taking the catalog from 911 to 854 —
rather than leaving them to dangle untraced against code that no longer
exists.

Verified locally: full `ctest` suite passes (60/60, down from 67/67 at
v0.82.0 — exactly the seven removed suites, nothing else regressed)
under both a plain Debug build and a manual
`-fsanitize=address,undefined` build (ASan+UBSan-clean across the full
suite; macOS ASan has no leak detector, so leak-checking itself is not
covered by this local run). A local `cfusa` v0.5.46 toolchain build:
`check`/`cyber`/`qualify`/`vuln` all exit 0 against a clean tree (no
`build*/` directories present — `cfusa` has no `.gitignore` awareness of
its own and will otherwise flag the vendored `unity` fetch-content
sources); `trace --req-coverage 100` reports 854/854 requirements traced
(100%, both metrics) — the tool's own advisory `UNTRACED` list drops from
four entries to three (`REQ-MDNS-007/008`, `REQ-RELAY-013`), since
`REQ-PQ-005` — one of the four pre-existing gaps prior milestones
flagged — no longer exists to be untraced. A local build of RELAY's own
`relay conform --strict` against the `-DRELAY_BUILD_CLI=ON` CLI target
reports PASS.

### 84. RELAY adapter rework (v0.84.0) ✅

- **REPLACE** `relay/relay.h` + `adapt.c`: RELAY's generic
  `Message`/`ToMessage()`/`FromMessage()` envelope assumes one generic
  command/response shape; TC18's 13 heterogeneous, fixed-shape endpoint
  types don't map onto it without per-endpoint-type translation rules
  RELAY itself doesn't yet define. This milestone proposes an interim
  per-endpoint-type message mapping scoped to this repo, and opens the
  upstream conversation with `SoundMatt/RELAY` (a GitHub issue against
  that repo, not an edit to it — per this project's own cross-repo
  policy) rather than assuming a resolution unilaterally.

**Done (v0.84.0)**:

- **`relay/relay.h`**: confirmed protocol-agnostic and structurally
  unaffected by the TC18 replacement (§18.2's Message/Context/Channel/
  Caller surface has no zone/command-shape assumption baked in) — the
  only change is bumping `RELAY_SPEC_VERSION` from the stale `"1.12"` to
  the current released `"1.14"` (v1.13 was a deep-audit fix pass; v1.14
  expanded the §13.7.2 module-name registry this project's own
  module-naming reconciliation, issue #87, already consulted), an
  incidental cleanup while this area was already being touched.
- **`include/rcp/adapt.h` + `src/adapt.c`**: fully REPLACEd, dropping
  every dependency on the retired `rcp_zone_t`/`rcp_command_t`/
  `rcp_response_t`/`rcp_status_t`/`rcp_controller_t` types (still defined,
  untouched, in `rcp.h` for `rcp.c`/`wire.c`/`mock.c`/`sim.c`/
  `tests/legacy_mock.*`'s own sake — this milestone is the *last*
  consumer of them anywhere in `src/` to move off). The interim
  per-endpoint-type mapping this milestone proposes:
  - `rcp_adapt_op_t`: a flat 18-value operation opcode, one entry per
    distinct wire request/response shape (finer than "endpoint type" —
    GPIO's read and write pack/unpack entirely different fields), each
    tagged with the endpoint-type family (`rcp_adapt_ep_kind_t`, 13
    values — 12 codec kinds, since PWM splits into PWM_OUT/PWM_IN, plus
    discovery — reconciling the roadmap's own "13 heterogeneous endpoint
    types... plus discovery and the register-map/lifecycle surface"
    phrasing against what actually has an ACF-level encode/decode pair to
    dispatch to; regmap.h/lifecycle.h confirmed to have none of their
    own, per regmap.h's own "unlike avtp.h/acf.h" note) it belongs to.
  - `rcp_message_to_request()`/`rcp_response_to_message()`: the new
    `FromMessage()`/`ToMessage()`-equivalent pair, each taking the opcode
    explicitly and dispatching via a per-opcode switch against the
    matching `ep_*.h`/`discovery.h` encode/decode function, per a
    documented field table (`relay_message_t.payload` always carries the
    operation's own natural wire-format bytes — raw tx/rx data or a
    fixed-width big-endian scalar; every other field is a decimal-string
    `meta["rcp.<kind>.<field>"]` entry, rekeying the old
    zone/command-type convention to endpoint-type/field per the
    milestone-80 precedent). CAN XL's two frame formats are out of this
    interim mapping's scope (ep_can.h's own encoder already rejects a
    missing `xl_header`, which this mapping never builds, for exactly
    those two formats — a naturally-enforced, not silently-mishandled,
    scope boundary, mirroring `RCP_EP_GPIO_WRITE_RESERVED6`'s own
    precedent).
  - `rcp_adapt()`: now wraps `avtp.h`'s pre-existing
    `rcp_avtp_transport_t` (the "send/recv one already-framed AVTPDU"
    primitive every endpoint type already assumes underneath its own
    ACF-level codec) rather than a retired `rcp_controller_t`, binding one
    `Caller` to exactly one endpoint address and endpoint-type kind —
    reusing, not reinventing, `shmem.h`'s `rcp_shmem_avtp_pair_new()`
    (milestone 78) as the in-process test double. `send()`/`call()` fail
    with a new `RCP_ADAPT_ERR_ENCODE` if a message's
    `meta["rcp.adapt.op"]` doesn't name an operation belonging to the
    bound kind.
  - `subscribe()` always returns a new `RCP_ADAPT_ERR_NOT_SUPPORTED`: TC18
    defines no generic periodic Status-equivalent stream for any endpoint
    type to forward, the same conclusion milestone 79's `deadline.c`
    REPLACE already reached for the same reason.
  - `rcp_errc_to_relay_errc()` (§5.2 error wrapping) is unchanged.
- **Upstream coordination**: opened
  [`SoundMatt/RELAY#66`](https://github.com/SoundMatt/RELAY/issues/66),
  proposing that RELAY's own spec (§15.7.5 and/or the `Adapt()`
  requirement) acknowledge the "several heterogeneous, fixed-shape
  request/response pairs, no shared generic envelope" case and point
  bindings toward a documented per-operation meta-key convention plus a
  narrower-than-\"the-whole-protocol\" `Caller` binding — filed as a
  GitHub issue against that repo, per this project's own "never edit
  other repos, file issues only" cross-repo policy, not assumed
  resolvable unilaterally here.
- **`tests/test_adapt.c`**: rewritten from scratch against real endpoint
  types, dropping its `tests/legacy_mock.h` dependency (the pattern every
  other milestone-77–83 satellite test rewrite already used) — direct
  `rcp_message_to_request()`/`rcp_response_to_message()` field-mapping
  coverage for a representative op from each shape family (GPIO's
  fixed-width scalar, SPI's channel-selector-plus-raw-bytes, I²C's
  plain-raw-bytes, UART's required-read-size, CAN's XL-rejection, MDIO's
  addressing-plus-packed-words, Wakeup's no-timed-variant, discovery's own
  NTSCF-framed shape), plus end-to-end `rcp_adapt()` vtable coverage
  (`send()`/`call()` round-tripped through a real `rcp_shmem_avtp_pair_new()`
  pair, `subscribe()`'s not-supported result, close idempotency, retain/
  release, and the §5.2 error-wrapping tests) — `tests/CMakeLists.txt`
  updated to match (`test_adapt` no longer links `legacy_mock.c`; with
  this milestone landed, no satellite decorates the legacy
  `rcp_controller_t`/`rcp_registry_t` vtables any more, so `mock.h`'s and
  `tests/legacy_mock.h`'s own file-header "not-yet-migrated" lists are
  updated to reflect that — whether to retire `tests/legacy_mock.h`
  itself, now that only its own test and the two benchmarking tools still
  use it, is left an open follow-up call, not decided unilaterally as
  part of this milestone's own "REPLACE relay.h + adapt.c" scope).
- **`.fusa-reqs.json`**: `REQ-RELAY-005` through `-012` rewritten in place
  to describe the new per-operation mapping/`Adapt()`/`send()`/`call()`/
  `subscribe()` behavior (same 17-entry `REQ-RELAY-*` count as before —
  no requirement added or orphaned); `REQ-RELAY-017`'s `//cfusa:req` tag
  restored in the rewritten `adapt.c` (behavior itself unchanged).

Verified locally: full `ctest` suite passes (60/60, unchanged from
v0.83.0) under both a plain Debug build and a manual
`-fsanitize=address,undefined` build (ASan+UBSan-clean across the full
suite). A local `cfusa` v0.5.46 toolchain build: `check`/`cyber`/
`qualify`/`vuln` all exit 0 against a clean tree; `trace --req-coverage
100` reports 854/854 requirements traced and 512/512 functions annotated
(both metrics), with the tool's own advisory `UNTRACED` list back to its
prior three-entry steady state (`REQ-MDNS-007/008`, `REQ-RELAY-013`) — no
new gap introduced. A local build of `SoundMatt/RELAY`'s own `relay
conform --strict` (built from that repo's current `main`, v1.14) against
a local `-DRELAY_BUILD_CLI=ON` build reports PASS.

---
### Phase 22 — Safety/Security Re-certification
---

Deliberately last, per the Satellite Disposition table's reasoning:
re-deriving certification evidence from code that doesn't exist yet is
backwards. Spec basis for the *content* being re-derived: whatever Phases
13–21 actually built, not the superseded catalog.

### 85. Full re-certification pass (v0.85.0) ✅

- `.fusa-reqs.json`: full requirements catalog re-derived against the new
  protocol's actual behavior (register-map access control, lifecycle
  transitions, E2E safe points, endpoint request/response shapes) —
  starting the traceability count over, the same honest
  "intentionally-below-100%-until-everything-lands" posture the legacy
  program used at its own v0.2.0, not a claimed carry-over of the old
  314-requirement count.
- `HARA.md`/`.fusa-hara.json`: hazard analysis re-run against the new
  safety mechanisms (Phase 18's CRC32 safe points and safety-request
  MSB variants, Phase 19's power-mode model) rather than the retired
  watchdog/E2E-CRC-16 hazard set.
- `tara.md`/`.json`, `CYBERSECURITY.md`: threat model re-authored against
  the new attack surface (MACsec's role now that `tls.c` is deprecated,
  the new discovery/lifecycle bootstrap sequence as an attack surface).
- `tla/*.tla`, `FORMAL_VERIFICATION.md`: new TLA+ models for the Phase 14
  lifecycle state machine and the Phase 18 CRC32 safe-point/safety-request
  mechanism, replacing the retired Healthy/Degraded/Faulted watchdog and
  CRC-16 anti-replay models.
- `AUDIT_PACK.md`, README.md: rewritten to describe the shipped TC18
  protocol rather than the superseded Zone/Command model.

**Done (v0.85.0)**:

- **`.fusa-reqs.json`**: not a blind carry-over. Every one of the 854
  existing entries was individually re-audited against the source file
  its own `//cfusa:req` tag actually appears in (a per-prefix grep
  across `src/`/`include/rcp/`, not an assumption), and classified with
  a new `"scope"` field. 779 entries (`REQ-E2E`, `REQ-LIFECYCLE`,
  `REQ-RMAP`, `REQ-FRAG`, `REQ-DISC`, `REQ-PWRMODE`, `REQ-PWR`, every
  `REQ-<endpoint-type>` family, `REQ-WDG`, `REQ-DL`, `REQ-CFG`,
  `REQ-AUTH`, `REQ-RL`, and every other already-TC18-native
  requirement) are `scope: "tc18"` — this project's actual ISO 26262
  safety-case basis, confirmed to already describe Phase 13–21's real
  implemented behavior rather than needing rewriting from scratch (most
  of this catalog was kept incrementally accurate by each landing
  milestone, not left to rot). The remaining 75 entries
  (`REQ-ZONE`, `REQ-CMD`, `REQ-CMDSTRUCT`, `REQ-PRI`, `REQ-STATUS`,
  `REQ-CTRL`, `REQ-REG`, `REQ-RESP`, `REQ-STAT`, and `REQ-ERR-011`) are
  confirmed exclusively tied to the retired `rcp.h`/`rcp.c` and
  `tests/legacy_mock.*` surface (v0.84.0's own milestone already
  established that surface as the last consumer of those retired types
  anywhere in `src/`) — reclassified `scope: "legacy-compat"`, `level`/
  `asil` demoted to `QM`, and their `text` prefixed with an explicit
  out-of-safety-case note, rather than deleted (deleting them out from
  under a surviving `//cfusa:req` tag in code this milestone is not
  authorized to touch would only trade one inaccuracy for a dangling-
  reference one). `cfusa trace --req-coverage 100` still reports 854/854
  (100%, both metrics) — the tool's own advisory `UNTRACED` list
  unchanged (`REQ-MDNS-007/008`, `REQ-RELAY-013`).
- **`HARA.md`/`.fusa-hara.json`**: full replacement, not an edit. 11 new
  hazards (H-001..H-011) and safety goals (SG-001..SG-011), re-derived
  against `regmap.h`'s writer-authorization model, `lifecycle.h`'s state
  machine, `e2e.h`'s CRC32 safe-point/safety-request gate and per-stream
  watchdog, `power.h`'s WakeUp handshake, and `discovery.h`'s bootstrap
  claim — not the retired Zone/watchdog/CRC-16 set. ASIL letters
  computed via `cfusa hara asil` (four hazards resolve to ASIL-C/D,
  documented in the ASIL Determination Note). Two hazards (H-004
  replay, H-007 link-layer authentication) are recorded honestly as
  **open, unmitigated** rather than folded into the same "derogation"
  framing as an implemented-but-lower-rigor control — there is no
  mechanism in this library to fall short of for either.
- **`tara.md`/`.json`, `CYBERSECURITY.md`**: full replacement. Threat
  model re-derived against the actual TC18 attack surface: request
  injection/spoofing (`authz.c`), replay (now genuinely unmitigated —
  `include/rcp/e2e.h`'s own file header already flagged this gap before
  this milestone closed the documentation debt around it), rogue
  discovery/bootstrap claims (`discovery.c`'s first-claimant-wins
  model), link-layer eavesdrop/tamper (MACsec absent from this
  library's own scope), and request-flood DoS (`ratelimit.c`, still
  exempting safety-tagged requests by default). `CYBERSECURITY.md`
  restructured from a 5-layer TLS/anti-replay/rate-limit/firmware model
  to a 6-layer MACsec(deployment)/authz/E2E/discovery-claim/regmap-
  write-auth/rate-limit model, since firmware.c's removal (v0.83.0)
  left no OTA-transfer layer to describe.
- **`tla/*.tla`, `FORMAL_VERIFICATION.md`**: `WatchdogProtocol.tla`,
  `HealthStateMachine.tla`, and `AntiReplayGuard.tla` deleted outright
  (all three modeled a Healthy/Degraded/Faulted per-zone watchdog or a
  CRC-16 sequence-number replay window, neither of which this codebase
  implements any more), replaced with two original models:
  `LifecycleStateMachine.tla` (`NoSkipConfiguration`,
  `FieldLockMonotonicWhileConfigured`) and `E2ESafePoint.tla`
  (`SafetyRequestsSurvivePurge`, `NoUnsafeSafetyExecution`). Both run
  clean through TLC 2.19 locally (`Model checking completed. No error
  has been found.`) before this PR was opened; CI's
  `formal-verification` job updated to model-check the new filenames.
- **`AUDIT_PACK.md`, `README.md`**: full rewrite. `README.md` drops the
  "zonal control"/cpp-RCP-and-go-RCP-port-mirror opening in favor of the
  shipped TC18 header/lifecycle/quick-start surface (the quick-start
  example was compiled and run locally against a real build, not just
  written); the retired Zone/Command surface is documented under its
  own "Legacy API" section rather than presented as the library's
  primary interface. `AUDIT_PACK.md` similarly rewritten (v2.0.0),
  dropping the ASIL-D gap table's cpp-RCP-mirrored derogation framing
  in favor of this project's own current posture, and correcting a
  stale "branch coverage not instrumented" open item that a later,
  unrelated workflow change had already fixed without this document
  being updated to say so.
- **`SAFETY_PLAN.md`**: also updated (not itself listed in this
  milestone's own bullet scope above, but squarely the same "safety/
  security artifacts" bucket, and left blatantly self-contradictory —
  stale hazard IDs, a stale SG-001..SG-010 count, an already-fixed
  "check/lint non-blocking" claim — if skipped while every document it
  cross-references was rewritten around it).

Verified locally: full `ctest` suite passes under a plain Debug build
(existing suite unchanged — this milestone touches no `src/`/`tests/`
code, only requirements/safety/security artifacts, per its own REPLACE
scope from the Satellite Disposition table). A local `cfusa` v0.5.46
toolchain build: `check`/`cyber`/`qualify`/`vuln` all exit 0 against a
clean tree; `trace --req-coverage 100` reports 854/854 requirements
traced and 512/512 functions annotated (both metrics unchanged from
v0.84.0). Both new TLA+ specs verified via a real local TLC run
(`tla2tools.jar`, TLC2 2.19), not assumed correct from inspection.

### Phase 23 — Post-Replacement Conformance Fixes
---

Targeted fixes found by an external audit against the shipped Phase 13–22
TC18 replacement, each scoped to its own milestone rather than bundled,
following this project's existing one-fix-per-milestone precedent (e.g.
v0.44.0, v0.56.0).

### 86. GPIO/PWM_OUT write-semantics enum off-by-one (v0.86.0) ✅

`rcp_ep_gpio_write_semantics_t` (`ep_gpio.h`) and
`rcp_ep_pwm_out_write_semantics_t` (`ep_pwm.h`) mapped wire `evt[2:0]`
values 4/5/6 to ADD/SUB/Reserved — one slot earlier than the correct
4=Reserved/5=ADD/6=SUB assignment (extraction §4.5 Group C). Both header
comments already flagged this as an unconfirmed guess pending clarification
rather than presenting it as settled; a cross-check against cpp-RCP's own
`WriteSemantics` enum (independently derived from the same structured spec
extraction, and presented there as an authoritative reading rather than a
guess) confirmed the correct assignment and this project's own value was
off by one.

Fixed by renumbering both enums (`RCP_EP_GPIO_WRITE_RESERVED4` /
`RCP_EP_PWM_OUT_WRITE_RESERVED4` now occupy value 4; `_ADD`/`_SUB` shift to
5/6) and updating `rcp_ep_gpio_apply_write()`'s and
`rcp_ep_pwm_out_apply_write()`'s switch statements to match. Both enums
are named-constant `typedef enum`s consumed only by name throughout this
codebase (never a raw integer literal), so the fix is a pure
value-reassignment with no call-site changes required elsewhere. Added
regression tests exercising the *raw* wire-value casts (not just the named
constants) for evt = 4/5/6 against both endpoint types, so a future
accidental reordering of the enum's numeric values (as opposed to its
names) would still be caught. `.fusa-reqs.json`'s REQ-GPIO-012/REQ-PWM-008
titles/text updated to reference `_RESERVED4` instead of the old
`_RESERVED6` name; no requirement behavior changed, since both requirements
were always phrased against the named constant, not a raw evt value.

Verified locally: full `ctest` suite passes (60/60 suites, including the
new regression tests) under a plain Debug build.

### 87. Pin c-FuSa to v0.5.49 (v0.87.0) ✅

`ci.yml`'s and `release.yml`'s c-FuSa clone step were both still pinned to
`v0.5.46`, three patch releases behind upstream's actual latest
(`v0.5.49`) at audit time. CI stayed green against the stale pin the whole
time, so the gap wasn't visible from build status alone.

Bumped both pins to `v0.5.49`. Verified locally with a real v0.5.49
`cfusa` build (not assumed compatible): `check`/`lint`/`analyze`/`cyber`/
`qualify`/`vuln` all still exit 0 (0 errors), and every command
`release.yml` runs (`fmea --cyber`, `safety-case --gsn`, `release`, `sas`,
`sci`, `audit-pack`, `badge`, `qualify --format json`) also exits 0.
`trace --req-coverage 100` still reports 854/854 traced, 512/512 functions
annotated, unchanged from v0.5.46.

One real behavioral difference surfaced: `cfusa check`'s combined safety
category gained two new rule families not present in v0.5.46 --
`COUP002`/`COUP003` (coupling) and `COMP001` (complexity) -- accounting
for 60 additional findings, all `WARNING` severity. Neither `ci.yml` nor
`release.yml` invokes any `cfusa` subcommand with `--strict` anywhere, so
these new warnings do not change any job's exit code; recorded here for
visibility since a future `--strict` adoption would need to budget for
them.

### 88. cfusa-trace CI job now gates on --sec-tested too (v0.88.0) ✅

The `cfusa-trace` job only ran `cfusa trace --req-coverage 100`, which
gates metric 1 (requirement traceability, i.e. every requirement has a
`//cfusa:req` impl tag somewhere) and metric 2 (function annotation
density). `cfusa trace` also supports a third, independent gate,
`--sec-tested N`, covering a different metric entirely: whether every
requirement additionally has a `//cfusa:test` or `//cfusa:sec-test` trace
tag pointing at the code that actually exercises it. `--req-coverage` and
`--sec-tested` are separate gates in `cfusa`'s own implementation (an
invocation picks one or the other, not both), and this job only ever
invoked the first. RELAY spec §20.1.2 requires both metrics at 100% for
continuous conformance.

An audit found 13 requirements (`REQ-CANEP-023..027`, `REQ-CLI-006`,
`REQ-DISC-025..028`, `REQ-UART-029..031`) carrying an impl tag but no
test/sec-test tag anywhere in the tree -- each is genuinely exercised by
an existing test (confirmed by reading each test file, not assumed), just
never tagged. Added `//cfusa:test` tags for all 13 at each file's existing
top-of-file tag block, matching this codebase's established per-file
tagging convention. `REQ-CLI-006` (covering `cli/main.c`'s `main()`) has
no dedicated unity test -- `main()` is a two-line, subprocess-only entry
point unity's in-process harness cannot invoke directly -- so it's tagged
in `tests/test_cli.c` with an explanatory comment pointing at the
`relay-conform` CI job, which builds and runs the actual `c-rcp` binary
(exercising `main()` itself, not just `rcp_cli_run()`) against
`relay conform --strict`.

Added a new `cfusa trace --sec-tested 100` step to the `cfusa-trace` job,
alongside (not replacing) the existing `--req-coverage 100` step, so both
metrics are now independently hard-gated.

Verified locally: full `ctest` suite (60/60) unaffected (tag-only/CI-only
change, no source touched). `cfusa` v0.5.49 `trace --sec-tested 100`
reports 854/854 (100%, exit 0); `trace --req-coverage 100` still reports
854/854 traced, 512/512 functions annotated, unchanged. `check`/`lint`/
`analyze`/`cyber`/`qualify`/`vuln` all still exit 0.

### 89. RELAY_SPEC_VERSION tracks spec v2.0 (v0.89.0) ✅

`include/relay/relay.h` still declared `RELAY_SPEC_VERSION "1.14"`, stale
against the RELAY spec's current MAJOR (v2.0, a breaking change replacing
§15.5's RCP canonical types and §15.7.5's `ToMessage()`/`FromMessage()`
mapping outright, released after this project's own v0.84.0 "RELAY
adapter rework" milestone had already landed a pre-v2.0 interim design).

Reviewed v2.0's actual RCP-specific content against `rcp/adapt.c`'s
existing per-op mapping (milestone 84): `relay/relay.h`'s own
`relay_message_t` envelope was already structurally compatible (a generic
`id`/`payload`/`meta` shape, not the retired Zone/Command types v2.0
describes leaving behind). `rcp/adapt.c`'s mapping, however, is a richer,
endpoint-specific superset of v2.0's new base mapping, not a literal
implementation of it -- it never set `relay_message_t::id` at all (except
for `RCP_ADAPT_OP_DISCOVERY`, which sets it to a stream_id for its own
reasons), where v2.0 §15.7.5 requires a response `Message`'s `id` to be
the responding endpoint's `ByteBusID` as a decimal string.

Bumped `RELAY_SPEC_VERSION` to `"2.0"`. Fixed the concrete gap this
milestone's scope covers: `rcp_response_to_message()` now sets `msg.id`
to the decimal `byte_bus_id` string for every op except `DISCOVERY`
(whose own stream_id-based `id` is the multi-stream extension §15.7.5's
own closing paragraph explicitly sanctions, since discovery has no
single meaningful `ByteBusID` of its own to report). Implemented as a
thin wrapper (`rcp_response_to_message()`) around the existing per-op
switch (renamed to the static `response_to_message_impl()`), so the id
is set in exactly one place rather than duplicated across 17 branches.

`Meta["rcp.op"]`/`Meta["rcp.error"]` (the other two fields v2.0's base
mapping defines) are **not** added by this milestone -- `rcp.op` would
require threading a generic read/write classification through the
request-encode path's 18 branches (today selected via the richer
`rcp_adapt_op_t` opcode, not a generic verb), and `rcp.error` would
require every one of the 13 endpoint types' response decoders to
recognize and surface an ACF-level `err=1` response frame, which several
don't currently attempt to decode at all (they're built against a
successful-response frame shape only). Both are real, correctly-scoped
gaps, not overlooked -- tracked via a comment update to the still-open
upstream `SoundMatt/RELAY#66` (this project's own prior request for
RELAY spec guidance on exactly this "heterogeneous fixed-shape
sub-protocol" problem, filed against milestone 84), rather than either
silently left undocumented or claimed as done. A future milestone should
pick this up explicitly rather than assume it's covered here.

Verified locally: full `ctest` suite (60/60, plus a new assertion on the
GPIO-response test confirming `msg.id == "7"`, and the existing
discovery-response test confirming its own stream_id-based `id` is
unaffected). One pre-existing test (`test_rcp_spec_version_equals_relay_
spec_version`) had `"1.14"` hardcoded as an expected value; updated to
`"2.0"`. `cfusa` v0.5.49 `check`/`lint`/`analyze`/`cyber`/`qualify`/
`vuln` all exit 0; `trace --req-coverage 100` and `trace --sec-tested
100` both still report 854/854 (100%), unchanged.

### 90. Add CHANGELOG.md (v0.90.0) ✅

No `CHANGELOG.md` existed anywhere in the tree despite (at the start of
this milestone) 89 tagged releases, even though `ROADMAP.md` documents
real deprecate-and-remove-in-the-same-release cycles with no intervening
MINOR release (`v0.78.0` removing `tls.c`, `v0.83.0`'s seven-module
deprecation batch). RELAY spec §19.2 requires `CHANGELOG.md` to record
the deprecation, replacement, and planned/actual removal version for
exactly this kind of event, and no such machine-discoverable or
conventionally-located record existed -- only prose buried in
`ROADMAP.md`.

Added `CHANGELOG.md` at the repo root: one entry per tagged release
(generated from `ROADMAP.md`'s own per-milestone headers and this
repository's tag history, newest first, each linking back to `ROADMAP.md`
for full detail rather than duplicating it), plus a dedicated
"Deprecation & Removal Log" table up top recording both real
deprecate-and-remove events found (`tls` at v0.78.0; `prioqueue`/
`firmware`/`zonegroup`/`proxy`/`redundancy`/`federation`/`dyndata` at
v0.83.0) against §19.2's required fields (deprecation version,
replacement, removal version).

Also: added a `CHANGELOG.md` line to `CONTRIBUTING.md`'s PR checklist
(required whenever a change deprecates/replaces/removes something;
otherwise a one-line entry alongside the `ROADMAP.md` update suffices),
and a short "Changelog" section to `README.md` pointing at it -- per the
issue's own suggestion to treat `CHANGELOG.md` as an ongoing release-gate
requirement going forward, not a one-time retroactive document.

Verified locally: full `ctest` suite (60/60) unaffected (docs-only
change, no source touched). `cfusa` v0.5.49
`check`/`lint`/`analyze`/`cyber`/`qualify`/`vuln`/`trace --req-coverage
100`/`trace --sec-tested 100` all unchanged from the prior milestone
(this change touches no tagged/tested code).

### 91. Remove retired pre-TC18 placeholder protocol residue (v0.91.0) ✅

A cross-repo ecosystem audit (2026-07-30) found that, despite the TC18
AVTP/ACF/register-map/lifecycle/endpoint core having been this project's
real, primary protocol implementation since Phase 13, the *retired*
pre-TC18 placeholder protocol was still compiled into the shipped `rcp`
static library: `src/wire.c`/`include/rcp/wire.h` (a bespoke
length-framed codec keyed to an `'RC'` magic, carrying `Zone`/
`CommandType` fields), `src/sim.c`/`include/rcp/sim.h` (a "zone
controller simulator" built on that same retired model), and
`rcp.h`/`rcp.c`'s own `rcp_zone_t`/`rcp_command_t`/`rcp_response_t`/
`rcp_status_t`/`rcp_controller_t`/`rcp_registry_t` vtable-based object
model. Six test/benchmark targets existed solely to certify this
residue (`test_rcp`, `test_wire`, `test_sim`, `test_legacy_mock`,
`bench_mock`, `command_latency_test`, backed by `tests/legacy_mock.*`),
and `.fusa-reqs.json` carried ~90 dead `REQ-ZONE`/`REQ-PRI`/`REQ-CMD`/
`REQ-STATUS`/`REQ-CMDSTRUCT`/`REQ-RESP`/`REQ-STAT`/`REQ-CTRL`/`REQ-REG`/
`REQ-SIM`/`REQ-ERR-*` requirement entries tracing it. An orphaned
`tooling/zone_manifest_schema.json` for the retired zone registry (no
build target, test, or source consumed it) rounded out the residue.

Per RELAY spec §15.5 ("RCP now means the real ... TC18 ... not the
earlier RELAY-internal placeholder protocol ... There is no
compatibility shim"), removed outright: `src/wire.c`, `include/rcp/
wire.h`, `src/sim.c`, `include/rcp/sim.h`, `tests/legacy_mock.{h,c}`,
`tests/test_rcp.c`, `tests/test_wire.c`, `tests/test_sim.c`,
`tests/test_legacy_mock.c`, `tests/bench_mock.c`,
`tests/command_latency_test.c`, `tooling/zone_manifest_schema.json`,
and every CMakeLists.txt reference to them. Verified beforehand (grep
across every `.c`/`.h` file) that no satellite decorator
(authz/loan/ratelimit/watchdog/deadline/powerstate/e2e/faultinject/
observe/recorder/adapt) or any other surviving module had a real
(non-comment) dependency on the removed types -- they had all already
migrated to TC18 addressing (`rcp_avtp_addr_t`) at milestones 78-84, and
their remaining hits were historical migration-note comments only,
left untouched.

`rcp.h`/`rcp.c` were a **partial** removal, not a deletion: the
protocol-agnostic primitives every TC18 module actually shares --
`rcp_bytes_t` (+`rcp_bytes_dup`/`rcp_bytes_free`), the base `rcp_errc_t`
sentinels (minus the now-meaningless `RCP_ERR_ZONE_MISMATCH`),
`rcp_context_t` (+its inline helpers), and `RCP_SPEC_VERSION` -- were
kept in place rather than relocated to a new header, since every module
in the tree already includes `"rcp/rcp.h"` and a rename would have
touched dozens of unrelated files for no behavioral benefit.
`rcp_bytes_dup()`/`rcp_bytes_free()` had been tagged with
`REQ-CTRL-026`/`REQ-CTRL-027`/`REQ-STAT-004` -- retired-model-specific
requirements describing `rcp_controller_send()`/`rcp_status_t` payload
copying -- so those three were purged along with the rest of `REQ-CTRL`/
`REQ-STAT` and replaced with two new, honestly-scoped `REQ-CORE-*`
requirements describing the primitive itself, with a new
`tests/test_core.c` covering them plus the surviving `REQ-ERR-*`
sentinel/`rcp_strerror()`/`relay_strerror()` properties that `test_rcp.c`
used to test (that file's own coverage of the *removed* zone/command
surface was not ported forward).

Also found and fixed in the same pass: `src/platform.c` (mutex/condvar/
thread primitives) and `src/clock.c` (`rcp_monotonic_ms()`) -- both
generic, codebase-wide infrastructure with no dependency on the retired
model -- had been tagged with `REQ-CTRL-004`/`007`/`011`/`018`/`019`
(coincidental reuse of Controller-concurrency requirement IDs, the same
loose-tagging pattern already seen with `wire.c`'s reuse of `REQ-UDP-*`,
except those *did* remain valid post-removal since `udp.c` still
implements them -- these did not, since their only other consumer,
`tests/legacy_mock.c`, was also removed). Retagged as three new,
dedicated `REQ-PLATFORM-*` requirements with direct unit coverage in a
new `tests/test_platform.c` (previously only exercised indirectly
through the retired mock's own test suite).

`README.md`'s "Legacy API" section (which pointed at the now-removed
surface) was rewritten as "Removed legacy API", and the one arbitrary
zone-name string literal in `tests/test_adapt.c` (`"FrontLeft"`/
`"RearRight"`, used only as generic `relay_message_t.id` test values
with no zone-lookup semantics) was renamed to `"ep-gpio-0"`/`"ep-pwm-1"`
to stop implying a connection to the removed model.

Verified locally: full `ctest` suite (56/56 -- 60 minus the six removed
placeholder-certifying targets, plus the two new `test_core`/
`test_platform` targets), clean build with zero compiler warnings
(`-Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
-Wstrict-prototypes -Wmissing-prototypes`), `cfusa` v0.5.49
`check`/`lint`/`analyze`/`cyber`/`qualify`/`vuln` all exit 0 (warning/
info counts strictly lower than before, as expected from removing
code), `trace --req-coverage 100` and `trace --sec-tested 100` both
100% (771/771, down from 854/854 -- 88 requirements purged outright,
of which 5 had been retired-model-tagged despite covering otherwise-
generic, still-kept code, so they were re-created as 5 new,
honestly-scoped requirements: `REQ-CORE-001`/`002` and
`REQ-PLATFORM-001`/`002`/`003`), and RELAY's own `relay conform
--strict` still PASS against the built `c-rcp` CLI.

### 92. Fix E2E CRC32 timestamp width and acf_msg_length adaptation (v0.92.0) ✅

A cross-repo ecosystem audit (2026-07-30) found two E2E CRC32
wire-conformance defects in `src/e2e.c`, both critical/high severity
since the CRC guards safety-tagged request execution:

`rcp_e2e_compute_crc()` fed `avtp_timestamp` into the running CRC as 8
octets (`put_u64`), but the IEEE 1722 `avtp_timestamp` field this
module's own file header describes covering is 32-bit/4-octet --
matching how `avtp.h` itself already models the field (`uint32_t` in
`rcp_avtp_tscf_header_t`). The mismatch meant every E2E-protected frame
folded 4 extra always-zero octets into the checksum beyond what a
conformant peer computes, breaking interop regardless of any other
fix. Changed `avtp_timestamp` from `uint64_t` to `uint32_t` on
`rcp_e2e_compute_crc()`/`rcp_e2e_wrap()`/`rcp_e2e_unwrap()`, and the
internal CRC loop from an 8-byte `put_u64` fold to a 4-byte `put_u32`
one.

Separately, `rcp_e2e_wrap()` appended its CRC trailer to an
already-fully-encoded ACF frame without ever adapting that frame's own
`acf_msg_length` field (acf.h offset 1-2, big-endian) to account for
the trailer -- the length adaptation this module's own file header had
always documented as the caller's responsibility ("a caller assembles
the safety-protected payload... passes its length via
rcp_e2e_length_with_crc()...") was simply never implemented by
anything: no caller in the tree drives that documented alternate flow,
so the composed `rcp_e2e_wrap()`/`rcp_e2e_unwrap()` path -- the one
the file header also describes as valid -- was the only one actually
reachable, and it skipped the adaptation entirely. Fixed by having
`rcp_e2e_wrap()` itself own the adaptation: it now works on a private
copy of the caller's frame, increments that copy's acf_msg_length by
one quadlet (`RCP_E2E_CRC_LEN`, matching acf.c's own `payload_len`-only
convention for that field) via a new file-private
`adapt_acf_msg_length()` helper, computes the CRC over the *adapted*
copy, and appends the trailer -- fail-safe (returns a zeroed
`rcp_bytes_t`) if the given frame is under 3 octets (nowhere for the
field to live) or already too close to the field's 16-bit ceiling.
`rcp_e2e_unwrap()` mirrors this: it verifies the CRC against the
frame as received (already carrying the sender's adaptation), then
returns a freshly heap-allocated copy of the header-and-payload region
with acf_msg_length adapted back down by one quadlet, ready for acf.c's
`rcp_acf_decode_abb()`/`_decode_gbb()` unmodified.

This is a breaking API change to `rcp_e2e_unwrap()`: it previously
wrote borrowed pointers into the caller's own `frame` buffer
(`out_acf_frame`/`out_acf_frame_len`); it now takes one `rcp_bytes_t
*out_acf_frame` and writes an owned, heap-allocated copy the caller
must free with `rcp_bytes_free()` -- required because the returned
region's acf_msg_length differs from the corresponding bytes in the
input (adapted back down by 4), so it can no longer alias the input
buffer. No caller in this tree used the old borrowed-pointer API
(verified by grep -- only `tests/test_e2e.c` and `e2e.c` itself
referenced it), so no other module needed updating.

`e2e.h`'s file header (the "Coverage span and the length-accounting
pre-adjustment" section) is rewritten to describe the actual, now
correct, behavior instead of a design that was documented but never
implemented.

Added: `test_compute_crc_timestamp_is_4_octets_not_8` (pins the CRC
input width, not just its value, so a regression back to 8 octets
would fail even if it happened to match on all-zero test timestamps),
`test_wrap_too_short_for_length_field_fails_safe`,
`test_wrap_adapts_acf_msg_length_by_one_quadlet` (asserts the encoded
acf_msg_length actually changes by exactly `RCP_E2E_CRC_LEN`, and that
the caller's original frame is left untouched). Every existing
wrap/unwrap/compute_crc test was updated for the new signatures and to
use frames with a real (if synthetic) acf_msg_length field, since the
adaptation now needs one.

Verified locally: full `ctest` suite (56/56), zero compiler warnings,
`cfusa` v0.5.49 `check`/`lint`/`analyze`/`cyber` all exit 0, `trace
--req-coverage 100` and `trace --sec-tested 100` both still 100%
(771/771, unchanged -- no requirement family added or removed, only
`REQ-E2E-003/005/006/007/008/009`'s existing implementations changed
behavior), and RELAY's own `relay conform --strict` still PASS against
the built `c-rcp` CLI.

### 93. Add TC18 wire error-code table, fix CRC_ERROR naming (v0.93.0) ✅

A cross-repo ecosystem audit (2026-07-30) found this project shipped no
representation at all of the governing TC18 spec's own numbered wire
error-code table (c-RCP-03): every error type in the tree was a local
`rcp_errc_t` or module-local `rcp_<mod>_errc_t` enum with no wire
representation, and no code anywhere modeled the seventeen numbered
codes an RC Server actually places in a Response frame's err field to
tell an RC Client what went wrong. The audit also corrected an earlier
issue's speculation that the numeric values "may not be explicitly
given" -- they are, and several names that earlier issue assumed
(`HW_CFG_INCONSISTENT`, `EPs_NOT_IDLE`, `LOCKED_CONFIG_ACCESS`, etc.)
are not the spec table's real names.

Added `include/rcp/errors.h`/`src/errors.c`: `rcp_wire_error_t` assigns
the seventeen numbered codes 1-17 exactly as the spec's own table does,
plus a value-0 `RCP_ERROR_NONE` sentinel (this implementation's own
addition, not part of the spec's table) for "no applicable wire code".
`rcp_wire_error_string()` gives each a unique, non-empty description.
This header intentionally does not attempt to wire every internal
error enum in the codebase onto one of these seventeen codes -- most
internal errors (allocation failure, buffer-too-small) never reach the
wire at all, so most of the mapping work is either not applicable or a
larger module-by-module audit than this milestone's scope.

Separately (c-RCP-04): `src/e2e.c`'s `rcp_e2e_strerror()` hardcoded the
string "CRC_ERROR" for a CRC mismatch -- the spec's own prose name for
this condition, but not an entry in the spec's authoritative numbered
table, which instead assigns a CRC mismatch the code POCI_FAILURE
(12). Once `rcp_wire_error_t` existed to actually emit real wire
codes, hardcoding a name with no corresponding number would have meant
that whenever this project starts emitting real error-code values,
the CRC-mismatch path would have nothing to emit. Fixed the
`rcp_e2e_strerror()` text and added `rcp_e2e_wire_error()`, mapping
`RCP_E2E_ERR_CRC_MISMATCH` to `RCP_ERROR_POCI_FAILURE` and every other
`rcp_e2e_errc_t` value (both local framing outcomes with no wire
representation) to `RCP_ERROR_NONE`.

Verified locally: full `ctest` suite (57/57, plus the two new targets
`test_errors`/the two new `test_e2e` cases), zero compiler warnings,
`cfusa` v0.5.49 `check`/`lint`/`analyze`/`cyber` all exit 0 (one
`cfusa cyber` false-positive found and fixed along the way: `CFUSA-
CY009`'s naive substring match for weak-crypto function names fired on
a test function named `..._codes_have_..._values`, since "codes_have"
contains "des_" -- renamed the function rather than filing an upstream
c-FuSa issue for a one-line, purely-cosmetic identifier), `trace
--req-coverage 100` and `trace --sec-tested 100` both 100% (774/774),
and RELAY's own `relay conform --strict` still PASS.

### 94. CLI capabilities self-report honesty (v0.94.0) ✅

A cross-repo ecosystem audit (2026-07-30) found three related accuracy
problems in `c-rcp capabilities`'s self-report:

`"transports"` listed `"tls"` (c-RCP-05) despite `tls.h`/`tls.c` having
been DEPRECATE-removed outright at v0.78.0 (see this file's own
Deprecation & Removal Log in CHANGELOG.md) -- the self-report was
advertising a backend that no longer exists in the source tree at all,
not merely a non-functional stub. Dropped it. `"mock"`/`"udp"`/
`"shmem"`/`"tsn"` were independently verified (grep for `NOT_SUPPORTED`
across each backend's `src/*.c`) to all have real, non-stub
implementations and stay listed; `udp.c`'s Windows-path-is-stub-only
gap is real but platform-specific, not "doesn't exist", and is a
separate, smaller, already-tracked issue, not folded into this fix.

`RCP_CLI_ALL_IMPLEMENTED_OPTIONS` (c-RCP-07) was a single hardcoded
constant unconditionally OR-ing every bit in all three
`svr_implemented_options` feature groups together, despite its own
comment claiming the value named "one per group this build actually
implements" -- there was no per-feature predicate behind it at all.
Renamed to `RCP_CLI_IMPLEMENTED_OPTIONS` and rebuilt from three
independently-named per-group predicates
(`RCP_CLI_HAS_TIME_SYNC_API`/`_ENHANCED_CANCEL_API`/
`_COMPOUND_BUNDLES_API`). All three still evaluate to 1 today (no
build-time feature-flag/`#ifdef` mechanism exists yet to ever flip one
to 0), so the reported bitmask is unchanged -- the fix is structural
(a future conformance fix has exactly one named predicate to flip),
not a behavior change.

The audit also flagged (c-RCP-06) that `"features"` reports these
three groups without any wire-conformance evidence behind them, citing
prior findings against each. This milestone independently re-verified
all three directly against the current code rather than assuming the
prior findings still held: `request_timed.c`'s `presentation_time` is
indeed a plain `uint32_t` (encode/decode both operate on it as such);
`request_cancel.c`'s encoders (e.g. `rcp_cancel_encode_clear_all()`)
do hard-code the shared ACF header's byte 5 (`op`/`evt`/`ms`) to
`0x00`, zeroing `evt` unconditionally; `request_compound.c`'s
`repeat_count` is confirmed `uint8_t`, and its own header comment
already documents it as "round-tripped only" with no repetition state
machine. All three are real, confirmed gaps -- not fixed here (each is
a substantially larger wire-conformance fix, out of this milestone's
scope), but the capabilities self-report now says so explicitly:
`src/cli.c`'s `capabilities_json()` doc comment and a new README.md
"CLI capabilities self-report" section both state plainly that
`features` means "API surface present", not "TC18-conformant". The
RELAY capabilities JSON schema itself (`additionalProperties: false`,
`features` a flat `string[]`) has no field to carry this caveat on the
wire, so it lives in source/docs instead, per the audit's own
recommended fallback when qualifying the wire-visible bits themselves
isn't in scope.

Verified locally: full `ctest` suite (57/57, plus two new `test_cli.c`
cases asserting `tls` is absent and the four real transports are
present), zero compiler warnings, `cfusa`
`check`/`lint`/`analyze`/`cyber` all exit 0, `trace --req-coverage 100`
and `trace --sec-tested 100` both still 100% (774/774, unchanged --
this milestone changed no requirement's implementation surface, only
its self-report accuracy and internal structure), and RELAY's own
`relay conform --strict` still PASS against the built `c-rcp` CLI
(confirming the schema still validates without `"tls"`). Also fixed in
passing: README.md's Headers table still listed the removed `<rcp/
sim.h>` from the v0.91.0 retired-protocol removal -- replaced with
`<rcp/errors.h>` (added v0.93.0), which the table had not yet listed.

### 95. CI hardening: ASan/UBSan job, real coverage regression gate (v0.95.0) ✅

A cross-repo ecosystem audit (2026-07-30) found two CI gaps in
`.github/workflows/ci.yml`:

c-RCP-N2-04 (#131): the pipeline built only Release and coverage Debug
configurations and ran MISRA/CERT static lint plus static analysis,
but no job ever compiled or ran the test suite under a dynamic memory-
safety sanitizer. For a C library performing untrusted network-frame
parsing (`acf.c`, wire-format decoders, `discovery.c`, `scheduler.c`
all index into attacker-controlled buffers via declared-length fields)
this is a real verification gap: static analysis cannot itself prove
the absence of an out-of-bounds read or integer-overflow-driven UB on
a malformed/boundary input. Added a `sanitizers (ASan/UBSan)` job:
builds with `-fsanitize=address,undefined -fno-sanitize-recover=all`
on `clang-14`/Ubuntu and runs the full `ctest` suite (`ASAN_OPTIONS=
halt_on_error=1:abort_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1`) --
a hard gate, no `|| true`. Verified locally first (macOS/AppleClang,
same flags): 57/57 tests pass clean under both sanitizers, so this
gate is expected to land green, not just theoretically correct.

c-RCP-N2-05 (#132): the "Structural coverage report (DO-178C)" step
and the full-compliance-report steps all appended `|| true`, so a
DAL-B structural-coverage shortfall (this project measures ~94% line /
~78% branch against DAL-B's 100%/100% requirement) never failed the
build, while `coverage-report.json` itself ships as a self-declared
FAIL artifact nothing consumed (c-RCP-11, #123 -- still open; closing
that gap fully means raising branch coverage to 100%, out of this
milestone's scope). Rather than simply removing `|| true` from the
DAL-B step (which would turn every future CI run red until DAL-B
itself is met -- not actionable in this milestone), split it in two:
the DAL-B report generation step stays best-effort and explicitly
documented as such (still produces the honest FAIL-against-DAL-B
artifact for the record), and a new "Coverage regression gate" step
runs `cfusa coverage --lcov coverage.info --threshold 88` (no `--dal`,
so it checks line coverage only) as a genuine hard gate -- 88% chosen
with several points of margin under the ~94% currently measured
(verified locally: 93.72% line via a local lcov/llvm-cov-gcov capture)
to absorb cross-toolchain (gcc-12 in CI vs. this verification's
llvm-cov) variance without flaking. This is a real, previously-entirely-
absent regression-prevention floor, not a DAL-B compliance claim --
DAL-B itself remains open, tracked via c-RCP-11/#123, not closed here.

Verified locally: full `ctest` suite unaffected (57/57), `cfusa`
`check`/`lint`/`analyze`/`cyber` all exit 0, YAML syntax validated
(`python3 -c "import yaml; yaml.safe_load(...)"`), and the new
sanitizer build + coverage-threshold command both verified to pass
locally before being wired into CI as hard gates.

Follow-up, found by the new `sanitizers` job on its first real CI run
(macOS's AddressSanitizer doesn't implement LeakSanitizer, so this
escaped local verification entirely): `tests/test_adapt.c` leaked 2456
bytes across 28 allocations in `rcp_adapt()`-wrapped
`rcp_avtp_loopback_transport_new()`/`rcp_shmem_avtp_pair_new()`
transports. Root cause: `rcp_adapt()` takes its own retained reference
to the transport it wraps (`adapt.c`: `rcp_avtp_transport_retain()` on
construction, `_release()` on `rcp_relay_caller_release()`) rather than
sole ownership -- the original creator's own reference (refcount 1 on
return, per `avtp.h`'s documented contract) is a separate obligation
the caller must still release. Ten test functions created a transport,
released only the `rcp_relay_caller_t` wrapping it, and never released
their own original transport reference. Fixed by adding the missing
`rcp_avtp_transport_release()` call to each of the ten (verified with
macOS's `leaks --atExit` against the plain, non-sanitized build, since
LeakSanitizer itself isn't available locally: 0 leaks before merge).
This was a test-only resource leak, not a product-code defect -- no
`src/*.c` file was touched by this fix.

### 96. Add SPDX-License-Identifier source headers (v0.96.0) ✅

A 2026-07-30 ecosystem audit found (c-RCP-15) zero
`SPDX-License-Identifier` hits across every `.h`/`.c` file in the
tree, despite the repo shipping a per-version `c-RCP-<version>.spdx.json`
SBOM (release.yml/`cfusa release`) making per-file license claims --
MPL-2.0 Exhibit A recommends a per-file notice, and without one the
SBOM's claims have no source-level tag to cross-check against.

Added `/* SPDX-License-Identifier: MPL-2.0 */` as line 1 of every
first-party file across `include/rcp/`, `include/relay/`, `src/`,
`cli/`, and `tests/` (178 files total) -- ahead of any existing
`//cfusa:req`/`//cfusa:test` tag block or file-header comment, which
cfusa's own tooling tolerates without issue (verified: `trace
--req-coverage 100`/`--sec-tested 100` both still 100%, 774/774,
unchanged after the header insertion). Added a new `spdx-headers` CI
job (a small dedicated grep-based check, since no existing c-FuSa lint
rule covers this) enforcing the header's presence on every matching
file going forward, per the audit's own "enforce via CI lint"
recommendation.

Verified locally: full `ctest` suite (57/57, this milestone touches no
executable code), zero compiler warnings, `cfusa`
`check`/`lint`/`analyze`/`cyber` all exit 0, `trace --req-coverage 100`
and `trace --sec-tested 100` both 100% (774/774, unchanged), and the
new `spdx-headers` check's own logic verified locally against the full
tree (0 missing).

### 97. Re-verify HARA ASIL audit finding; fix real hazard-count miscount (v0.97.0) ✅

A 2026-07-30 ecosystem audit (c-RCP-13) claimed HARA.md's ASIL column
was systematically over-rated: applying a "S+E+C, sum >=7 is A, >=8 is
B, >=9 is C, >=10 is D" heuristic to the recorded Severity/Exposure/
Controllability classes, it computed every hazard one-to-two bands
below what HARA.md/`.fusa-hara.json` actually record (e.g. H-001
S3+E4+C2 "=9 -> ASIL-C" vs. the recorded ASIL-D; H-005 S3+E3+C2 "=8 ->
ASIL-B" vs. the recorded ASIL-D).

Before changing any hazard's ASIL letter, re-derived all 11 directly
against the actual ISO 26262-3:2018 Table 4 -- via `cfusa hara asil
--severity N --exposure N --controllability N`, this project's own
referenced authoritative source (HARA.md's ASIL Determination Note:
"ASIL letters are computed via `cfusa hara asil`"), which implements
the real table (a genuine 3×4×4 lookup, `src/asil.c` in c-FuSa, not a
linear sum) rather than the audit's simplified arithmetic proxy. Ran
all 11 hazards' recorded S/E/C triples through it: **every one of the
11 currently-recorded ASIL letters is already correct** against the
real Table 4. The audit's finding does not hold -- its own summation
heuristic simply does not match ISO 26262-3:2018 Table 4's actual,
non-linear shape (e.g. S3/C2 rows escalate to ASIL-C/D far faster than
a linear S+E+C sum would predict, since Table 4 weights severity and
controllability non-additively). **No hazard's ASIL rating was
changed** as a result of this milestone.

Re-verifying line-by-line surfaced a real, different bug the audit's
own restated evidence had actually been pointing at without diagnosing
correctly: the ASIL Determination Note's own prose said "**Four**
hazards resolve to ASIL-C or ASIL-D" but then named **five** (H-001,
H-005, H-003, H-007, H-008) -- and H-007 does independently compute
ASIL-C per the same tool (S3/E2/C2), confirmed by the document's own
later prose describing H-007 as "it computes ASIL-C but has no
ASIL-B-or-higher control implemented at all". The Residual Risks
table's own summary row repeated the identical four-hazard undercount
("Four hazards (H-001, H-003, H-005, H-008)..."), independently
omitting H-007 from its own list too. Fixed both occurrences to say
"Five" and include H-007. `.fusa-hara.json` needed no change -- its
per-hazard `risk.severity`/`risk.exposure`/`risk.controllability`/
`risk.asil` fields already matched HARA.md's table exactly for all 11
hazards, confirmed before making any edit.

This milestone is intentionally honest about a prior audit claim not
holding up under direct re-verification against this project's own
authoritative tool, per the fix-pass's own ground rule not to
overstate what changed -- see the closing comment on GitHub issue
c-RCP-13/#125 for the same explanation, posted alongside this
milestone's PR.

Verified locally: `cfusa hara asil` re-run for all 11 hazards'
recorded S/E/C triples (documented inline above), full `ctest` suite
unaffected (57/57, this milestone touches only `HARA.md`), `cfusa`
`check`/`lint`/`analyze`/`cyber` all exit 0 (this milestone touches no
tagged/tested code, so `trace --req-coverage 100`/`--sec-tested 100`
are unaffected).

### 99. Bump c-FuSa pin to v0.5.50; fix real HARA ASIL over-classification (v0.99.0) ✅

Routine upstream check: `ci.yml`/`release.yml`'s c-FuSa clone step was
pinned to `v0.5.49`, one patch release behind upstream's actual latest
(`v0.5.50`). Bumped both pins.

`v0.5.50`'s own release notes disclosed a real, previously-unknown bug:
`c-FuSa`'s shared `cfusa_compute_asil()` table (`src/asil.c`) had
over-assigned ASIL in 19 of its 36 S×E×C cells (every S2 row except E1,
and every S3 row) for its entire history up to and including `v0.5.49`
— see <https://github.com/SoundMatt/c-FuSa/releases/tag/v0.5.50>. A
local build of the new `cfusa` (not assumed compatible) surfaced a real
new `HARA006` **ERROR** running `cfusa check` against this repo's own
`.fusa-hara.json`: all 11 recorded hazard ASIL letters disagreed with
what the corrected table now computes from their recorded
severity/exposure/controllability, because all 11 had been computed
against the buggy pre-`v0.5.50` table.

This directly supersedes milestone 97's (v0.97.0) "Re-verify HARA ASIL
audit finding" conclusion. That milestone re-ran the same 11 recorded
S/E/C triples through `cfusa hara asil` (then `v0.5.49`) and found them
self-consistent, correctly rejecting a contemporaneous external audit's
simplified "S+E+C sum" heuristic as not matching ISO 26262-3:2018 Table
4's real, non-linear shape — but the *tool* computing that "real" table
was itself wrong at the time, so self-consistency with a buggy tool
was never the same thing as correctness. Independently re-derived all
11 hazards by hand against the actual ISO 26262-3:2018 Table 4 (a
genuine 3×4×4 lookup, not trusting either `cfusa`'s old or new output
blindly, per this fix-pass's standing rule to verify tooling against
the primary standard) before accepting the corrected values: every one
of the 11 hand-derived results matches `cfusa hara asil` v0.5.50's
output exactly. (The external audit's own linear-sum arithmetic, as it
turns out, *is* an exact reproduction of Table 4's real S×E×C shape for
all 36 cells — `sum <= 6` → QM, `7` → A, `8` → B, `9` → C, `10` → D —
so its numeric conclusions were right all along; milestone 97 was
correct only that "heuristic" was the wrong word for it.)

Corrected `.fusa-hara.json`'s `risk.asil` and `safetyGoals[].asil`
fields for all 11 hazards/safety-goals against `cfusa hara asil`
v0.5.50 output: H-001 ASIL-D → ASIL-C, H-002 ASIL-B → ASIL-A, H-003
ASIL-C → ASIL-B, H-004 ASIL-B → ASIL-A, H-005 ASIL-D → ASIL-B, H-006
ASIL-B → ASIL-A, H-007 ASIL-C → ASIL-A, H-008 ASIL-D → ASIL-B, H-009
ASIL-B → ASIL-A, H-010 ASIL-B → ASIL-A, H-011 ASIL-A → QM. Updated
`HARA.md` (hazard table, ASIL Determination Note, Safety Goals table,
Residual Risks table) to match, rewrote the ASIL Determination Note to
explain the correction and supersede milestone 97's conclusion rather
than silently overwrite it, and updated `SAFETY_PLAN.md` and
`AUDIT_PACK.md` (retitled its §2 from "ASIL-D Gap Analysis" to
"ASIL-C Gap Analysis" — no hazard reaches ASIL-D any more) and
`tara.md`'s TS-001 row, all of which cited the old four/five-hazard
ASIL-C/D count. `.fusa-reqs.json`'s 774 per-requirement ASIL levels
were deliberately left untouched — they are independently assigned per
requirement scope, not derived from hazard S/E/C, and are outside
`HARA006`'s check.

H-001 remains the sole hazard exceeding the ASIL-B baseline (now at
ASIL-C, not ASIL-D); H-003/H-005/H-008 now land exactly at the ASIL-B
baseline and no longer need a decomposition argument; H-007 drops to
ASIL-A but remains a genuinely open, unmitigated item regardless of
its ASIL letter (MACsec is still out of this library's scope); H-011
drops out of ASIL scope entirely (QM).

Verified locally: fresh `cfusa` v0.5.50 built from source
(`cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake
--build build --parallel`), `cfusa check` on this repo's own tree
0 errors/exit 0 (previously 11 `HARA006` errors against the
uncorrected data, confirmed reproduced before fixing).
`lint`/`analyze`/`cyber`/`qualify`/`vuln` all still exit 0. Every
command `release.yml` runs (`fmea --cyber`, `safety-case --gsn`,
`release`, `iso26262`/`iec61508`/`do178`/`iec62443` gap reports,
`coverage`, `sas`, `sci`, `audit-pack`, `badge`, `report`,
`qualify --format json`) also exits 0. `trace --req-coverage 100` and
`trace --sec-tested 100` both still 100% (774/774), unchanged. Full
`ctest` suite 57/57 passed (this milestone touches no executable
source, only CI config and safety documentation/data). c-FuSa's own
known `qualify` tool bug — `qualificationBadge` in
`qualify-report.json` always reports `"unqualified"` regardless of
actual pass count — is still present in v0.5.50 (not fixed upstream);
not hand-edited, since it's a generated artifact CI regenerates every
release.

### 100. TC18 `byte_message_info` conformance fix: real bit layout, quadlet `acf_msg_length`, multi-request dispatch (v0.100.0) ✅

A cross-repo external gap audit (2026-07/31 pass, coordinated against
`go-RCP`/`cpp-RCP`/`rust-RCP` and the real TC18 v0.5.1_RC specification
text) found `acf.h`/`acf.c`'s `byte_message_info` header was, by its own
file-header's admission, this implementation's own invented byte
layout, never reproduced from the specification: `acf_msg_type`
occupied a full leading octet instead of being packed with a 9-bit
`acf_msg_length` into the first 16-bit word; `acf_msg_length` was a raw
16-bit octet count of the payload alone, not the specification's 9-bit
quadlet count spanning the whole message (header+timestamp+payload+
pad); `byte_bus_id` was a flat 8-bit field at a fixed offset instead of
an 11-bit field split across two octets; and the `pad`/`mtv`/`hs`/`cs`/
`rsp`/`err`/`evt`/`op`/`ms` flag octets were grouped differently from
the specification's own octet 2/4/6 layout. **No prior tagged release
of this implementation was wire-compatible with a real TC18 peer, or
with this project's own `go-RCP`/`cpp-RCP` ports, at the ACF header
level.**

Rewrote `rcp_acf_byte_message_info_t` and its encode/decode pair from
the specification's real Figure 7 / Table 4 layout, field by field, bit
by bit, using `go-RCP`'s already-conformant `acf/message.go` as the
reference pattern (translated into idiomatic C, not a literal port —
see `acf.h`'s file header for the exact octet-by-octet layout table).
New shared `rcp_acf_pack_header()`/`rcp_acf_unpack_header()` helpers
are now the single source of truth for this bit-packing, used by both
`acf.c`'s own `encode_abb()`/`encode_gbb()`/`decode_abb()`/`decode_gbb()`
and by the five request-kind modules (`request_compound.c`,
`request_cancel.c`, `request_timed.c`, `request_triggered.c`,
`request_chained.c`) that build a raw ACF_GBB header themselves to
repurpose `message_timestamp` — previously each of those five
duplicated the (wrong) bit-packing logic by hand.

`acf_msg_length` is now correctly quadlet-counted: `rcp_acf_encode_abb()`/
`_encode_gbb()` compute the true unpadded header(+timestamp)+payload
length, round it up to a quadlet boundary via the new
`rcp_acf_pad_len()`, and encode both the resulting pad-octet count (into
the `pad` field, per Table 4 — no longer a caller-supplied,
round-tripped-only value) and the quadlet count (into `acf_msg_length`)
themselves; decode reads both back and derives payload length as
`quadlets*4 - header_len - pad`, rather than trusting a raw octet
count. `e2e.c`'s `adapt_acf_msg_length()` (the +1-quadlet adjustment
`rcp_e2e_wrap()`/`_unwrap()` apply around the CRC32 trailer) is updated
to read/write the field at its new bit position instead of the old
16-bit offset-1-2 field, preserving `acf_msg_type` untouched in the
process. `scheduler.c`'s `rcp_sched_split_frame_members()` (the
multi-request-per-frame primitive) is updated identically.

Hand-verified against the specification's own worked examples before
trusting any of this: Figure 19 (a single ACF_ABB with a CRC32 safe
point, header+6-byte payload+2-byte pad+4-byte CRC = 20 octets = 5
quadlets) and Figure 20 (a single ACF_GBB, header+timestamp+7-byte
payload+1-byte pad+4-byte CRC = 28 octets = 7 quadlets) both reproduce
exactly (`acf_msg_length` 0x05 and 0x07 respectively, full byte-for-byte
match) — pinned as golden-vector tests in `tests/test_acf.c`
(`test_golden_figure19_abb_prelcrc_quadlets_and_pad`/
`test_golden_figure20_gbb_prelcrc_quadlets_and_pad`).

Secondary fixes bundled with the same header rework (same root cause,
same file):

- `mtv` is now a single wire bit (`RCP_ACF_MTV_UNTIMED`/`_VALID`), not a
  2-bit field with an invented third `RCP_ACF_MTV_UNCERTAIN` state that
  had no TC18 wire encoding.
- `read_size_or_segment_num` widened from `uint8_t` to `uint16_t` to
  actually carry its full 12-bit wire range (0-4095), not just 0-255.
- `op` (a single wire bit: 0=read, 1=write) can no longer losslessly
  round-trip this project's own 3-state `rcp_acf_op_t` convenience enum
  (`NONE`/`WRITE`/`READ`, used by endpoints like `ep_wakeup.c` for
  command-shaped messages with no register read/write semantics of
  their own) — `RCP_ACF_OP_NONE` now encodes identically to
  `RCP_ACF_OP_WRITE` on the wire (there is no third wire state to
  invent one for) and a decoded header's `op` is therefore always
  `READ` or `WRITE`, never `NONE`. See `acf.h`'s `rcp_acf_op_t` doc
  comment.
- Decoding a byte_bus_id whose wire value exceeds 255 (this project's
  `rcp_byte_bus_id_t` is 8 bits wide, matching `go-RCP`'s own identical
  limitation) now fails explicitly with a new `RCP_ACF_ERR_BUS_ID_OVERFLOW`,
  rather than silently truncating.
- `RCP_ACF_MAX_PAYLOAD` (previously 65535, from the old 16-bit length
  field) is now `RCP_ACF_GBB_MAX_PAYLOAD` (2028), with
  `RCP_ACF_ABB_MAX_PAYLOAD` (2036) added for the wider variant-specific
  bound. This has a real consequence for `ep_can.h`: a worst-case CAN XL
  new-payload frame's ACF-level encoding (2058 octets) no longer fits
  in a single unfragmented ACF message at all. The existing fragmented
  *response* path (`rcp_ep_can_encode_frame_response_fragmented()`) is
  unaffected (its `max_fragment_payload` is always well under this
  bound in practice) and is now the *only* way to carry a worst-case CAN
  XL frame; there is still no fragmented *request* counterpart
  (`rcp_ep_can_encode_frame_request()` has none), so a worst-case CAN XL
  new-payload *write* request cannot be sent at all as of this release —
  tracked as a follow-up, not this milestone's scope (see `ep_can.h`'s
  updated file header).

Separately, closed the TC18 §12.9.1.1 "RC Server shall support multiple
requests in one frame" gap: `scheduler.c`'s `rcp_sched_split_frame_members()`
existed and was unit-tested in isolation, but the only real dispatch
entry point (`mock.c`'s `rcp_mock_server_dispatch()`) processed exactly
one pre-decoded request against exactly one `byte_bus_id`. Added
`rcp_mock_server_dispatch_frame()`: given a raw, not-yet-split NTSCF/TSCF
payload, it splits it into its constituent ACF messages, decodes each
one's own `byte_bus_id`, and dispatches each individually via the
existing `rcp_mock_server_dispatch()`, writing one
`rcp_mock_frame_member_result_t` per member. A single-member frame
behaves identically to calling `rcp_mock_server_dispatch()` once
directly.

Not fixed this milestone (deferred, tracked as follow-ups): TC18
§11.3.1/§11.3.4's numbered wire error codes (`errors.h`'s
`rcp_wire_error_t`, complete since v0.93.0) are still never populated
onto an actual Error response's `byte_msg_payload` by any endpoint
module, the mock dispatch path, or `regmap.c` — wiring this in requires
a per-endpoint design decision (which local failure maps to which of
the 17 numbered codes) for each of `ep_gpio.c` and its ~9 siblings, a
materially larger and separately-scoped effort than this header rework,
matching this project's own established precedent of adding wire-error
mappings one at a time with their own audit (`rcp_e2e_wire_error()`,
v0.93.0) rather than in bulk. `coverage-report.json`'s self-reported
`Overall: FAIL` (below this project's own stated DAL-B thresholds) and
`qualify-report.json`'s three-way-inconsistent qualification signals
are both `cfusa`-generated artifacts CI regenerates every release (the
latter is `cfusa`'s own known `qualificationBadge` bug, noted already in
milestone 99 above) — the correct fix for both belongs in `c-FuSa`
upstream, not as a hand-edit here that the next CI run would silently
discard; not touched, per this project's standing policy against
editing (or hand-patching the generated output of) a sibling repo's
tooling. `ci.yml`'s `ilammy/msvc-dev-cmd@v1` mutable-tag pin was SHA-pinned
as a small, unrelated hardening fix bundled into this same release.

Updated `tests/test_acf.c` (golden vectors plus bit-position pins for
`rcp_acf_pack_header()`/`_unpack_header()`), `tests/test_e2e.c` (the
`adapt_acf_msg_length()` field-position tests), `tests/test_mock.c`
(five new `rcp_mock_server_dispatch_frame()` tests), and
`tests/test_ep_can.c` (the CAN XL request round-trip test's payload
size, since its previous 2048-byte worst case no longer fits — see
above; the existing fragmented-response worst-case test is unaffected).
Full `ctest` suite 57/57 passed. `.fusa-reqs.json` gained
`REQ-MOCK-019`/`REQ-MOCK-020` for the new dispatch-frame behavior.
`README.md`'s wire-interop claim now carries an explicit hedge pending
the deferred items above and live third-party interop testing.

### 101. Native Ethernet (L2) transport + real Annex J UDP conformance (v0.101.0) ✅

**Deferred, not forgotten**: milestone 59 (v0.59.0) named three concrete
`rcp_avtp_transport_t` carriers as this vtable's own reason to exist --
native Ethernet (EtherType `0x22F0`), IEEE1722-over-UDP/IP (Annex J), and
CAN(FD/XL)-as-network. Milestone 78 (v0.78.0, "Transport satellites")
delivered a real, from-scratch UDP transport but silently dropped native
Ethernet from its own scope without ever saying so; nothing in this
codebase has implemented it since (`find . -iname "*eth*"` outside
`.git`/build directories confirmed empty before this milestone started).
This milestone finally delivers it, and separately fixes a real
conformance gap the existing UDP transport has carried since v0.78.0:
CAN(FD/XL)-as-network remains the one still-unimplemented carrier from
milestone 59's original list, still tracked, not this milestone's scope.

**UDP Annex J conformance fix.** Cross-checked against two independent
public secondary sources (a Wireshark issue tracker discussion of the
real Annex J framing, and the COVESA Open1722 open-source reference
implementation's `Avtp_Udp_t` header struct, BSD-3-Clause) -- this
project has no access to the paywalled IEEE 1722-2016 standard text
itself, so both `udp.h`'s own file header and this entry say so
explicitly rather than presenting either source as primary-source-
verified. Both sources agree that Annex J framing prepends a 4-octet,
big-endian "encapsulation sequence number" ahead of the AVTPDU on the
wire, a field with no counterpart in native-Ethernet framing; `udp.c`'s
existing transport had neither that field nor a standard default
control-plane port. New, pure, socket-free `rcp_udp_annexj_wrap()`/
`_unwrap()` are the codec for that field, applied transparently inside
`rcp_udp_avtp_transport_dial()`/`_bind()`'s own `send()`/`recv()`; each
transport instance now maintains its own monotonically-incrementing
`uint32_t` send counter, and the most recently *received* sequence
number is exposed via the new `rcp_udp_avtp_transport_last_recv_seq()`
for callers who want it -- without inventing any receiver-side
gap/reorder-detection semantics for it, since neither secondary source
documents what those are meant to be. New `RCP_UDP_ANNEX_J_CONTROL_PORT`
(17221, the "Discrete"/control-plane port both sources agree on --
`RCP_UDP_ANNEX_J_CONTINUOUS_PORT`, 17220, is documented alongside it for
completeness but not used by anything in this module) and new
`rcp_udp_avtp_transport_dial_default_port()`/`_bind_default_port()`
convenience wrappers that fill it in; the explicit-port `dial()`/`bind()`
entry points are untouched for back-compat, and port `0` keeps its
existing, different, already-tested meaning ("OS-assigned ephemeral
port" for `bind()`) rather than being overloaded to also mean "use the
default control port" -- a new wrapper function was the safer,
non-breaking evolution here, in the same spirit as this project's
existing `_default_config()`-style convenience-wrapper convention
elsewhere. `RCP_UDP_AVTP_MAX_FRAME`'s receive-buffer sizing grew by
`RCP_UDP_ANNEX_J_SEQ_LEN` (4) to account for the extra framing; a
datagram that arrives shorter than that field itself is dropped and
polling continues, the same "nothing left in the kernel's own receive
queue to retry against" reasoning `udp.c` already used for an oversized
datagram.

**New native-Ethernet (L2) transport.** New `include/rcp/l2.h` +
`src/l2.c`, structurally parallel to `udp.h`/`udp.c`: another
`rcp_avtp_transport_t` implementation (`l2_avtp_vtable`), with the same
poll-rather-than-block `recv()` and "flag-then-real-teardown"
`close()`/`destroy()` split `udp.c` already established. Wire frame:
destination MAC (6 octets) + source MAC (6 octets) + EtherType `0x22F0`
(2 octets, big-endian) + the AVTPDU bytes directly -- deliberately no
Annex J encapsulation sequence number, which the UDP-only framing above
does not carry over onto this carrier. `rcp_l2_frame_encode()`/`_decode()`
are the pure, socket-free codec for that frame, unit-tested (`tests/
test_l2.c`) with no socket, no privilege, and no Linux requirement, same
as the new UDP codec functions above. The transport itself
(`rcp_l2_avtp_transport_new()`) is Linux-only (`AF_PACKET`/`SOCK_RAW`,
needs `CAP_NET_RAW` or root, documented plainly in `l2.h`'s own doc
comment): it opens a raw socket bound to a caller-named interface (e.g.
`"eth0"`), reads that interface's own hardware address via the
`SIOCGIFHWADDR` ioctl to use as the source MAC on every frame it sends
(never caller-supplied -- spoofing a different device's address by
accident is not a decision this module makes for a caller), and targets
a caller-supplied destination MAC (unicast or multicast -- deriving/
allocating a multicast MAC from a stream_id is the base IEEE 1722
standard's own algorithm, not implemented here, per this milestone's
explicit scope). `recv()` filters out `PACKET_OUTGOING` frames (a raw
socket, by default, loops a copy of its own transmitted frames back into
its own receive queue) so a transport never mistakes its own just-sent
frame for a reply from its peer. Every non-Linux build gets the same
fail-cleanly stub (`l2_stub_vtable`) `udp.c`'s own Windows stub already
established: `ok()` reports false, `send()`/`recv()` return
`RCP_ERR_CLOSED`, nothing crashes.

**Testing.** `tests/test_l2.c` (8 cases) covers the frame codec plus
graceful construction-time failure, unprivileged, on every platform.
`tests/test_udp.c` gained 7 cases for the encapsulation codec, monotonic
send-sequence observability via `last_recv_seq()`, and the new default-
port wrappers -- the existing UDP round-trip tests needed no changes at
all, since `send()`/`recv()` apply/strip the new framing transparently.
A new, deliberately-not-`ctest`-wired `tests/l2_veth_roundtrip.c` program
moves real AVTPDUs across two real `rcp_l2_avtp_transport_t` instances
bound to opposite ends of a real `veth` pair over real `AF_PACKET`
sockets, asserting byte-for-byte equality in both directions and
exercising `close()`-unblocks-a-concurrent-`recv()` for real -- neither
of which an unprivileged, cross-platform unit test can itself do. New
`ci.yml` job `l2-transport-veth` (`sudo ip link add veth0 type veth peer
name veth1`, elevated privileges, Linux-only, matching this project's
existing per-concern job-splitting style) builds and runs it. Full local
`ctest` suite 58/58 passing (57 prior + `rcp_l2`), clean under a manual
`-fsanitize=address,undefined` run of the same suite; the veth job itself
was not runnable in this development environment (no Linux host with
`CAP_NET_RAW` available locally) and is instead verified by CI on push.

New `REQ-UDP-015`..`019` (encapsulation codec, `send()`/`recv()`
integration, default-port wrappers) and `REQ-L2-001`..`010` (frame codec,
transport construction/`ok()`/`local_mac()`, `send()`/`recv()`/`close()`,
the non-Linux stub, and the missing-privilege path) added to
`.fusa-reqs.json`, each with a 1:1 `//cfusa:req`/`//cfusa:test` tag pair
hand-verified before pushing (no local `cfusa` toolchain in this
environment, the same note every prior milestone has made).

### 102. Conditional-request conformance: real wire sub-fields, and actually wiring them into dispatch (v0.102.0) ✅

**The gap this closes is two independent bugs stacked on top of each
other**, both across the whole conditional-request feature area (TC18
§11.2.2/§11.2.3: compound, compound-wait, triggered, chained, timed,
cancellation).

**Layer 1 -- the wire sub-field encodings were not conformant.** Every
one of these request kinds carries its own sub-fields in the eight
octets of an ACF_GBB's repurposed `message_timestamp` region, and every
one of them had those octets in the wrong place, the wrong width, or
missing entirely. Each was re-derived here from the specification's own
figures and field tables (Figure 8/Table 6, Figure 9/Table 7, Figure
10/Table 8, Figure 11/Table 9, Figure 12/Table 10, Figure 15/Table 13),
read from the PDF rather than from any prior summary:

- **Compound / compound-wait (0x0F/0x8F, 0x0B/0x8B)**: the correct layout
  is `start_state`(1) `next_state`(1) `sequencer`(1) `exec_delay`(2)
  `repetitions`(2) at offsets 1..7. This module packed `sequencer_index`
  as 16 bits at offset 1, shifting `start_state`/`next_state` two octets
  late, and squeezed `repetitions` into a single octet with a
  correspondingly wrong `0xFF` infinite sentinel instead of the
  specified two-octet `0xFFFF`.
- **Triggered (0x0E/0x8E)**: the correct layout is `trigger_source_ep`(1)
  `trigger_signal_nr`(1) `trigger_threshold`(1) `exec_delay`(2)
  `repetitions`(2). This module carried compound's
  `sequencer_index`/`start_state`/`next_state` here instead -- meaning
  the entire trigger-*selection* mechanism was absent: a triggered
  request had no way to say which trigger it was waiting on, and
  `rcp_triggered_tick()` gated on a sequencer state a triggered request
  does not have. This was the largest single change.
- **Timed (0x0A)**: the correct layout is a mandatorily-zero reserved
  octet at offset 1 and a 48-bit big-endian `presentation_time` spanning
  offsets 2..7. This module packed a 32-bit value starting at offset 1,
  overwriting the reserved octet and truncating the field's rollover
  period from ~3.25 days to ~4.3 seconds of nanoseconds.
- **Chained (0x01)**: the correct layout is `chain_exec_delay`(2) at
  offsets 4..5 with offsets 1..3 and 6..7 reserved-zero. This module
  invented `chain_length` and `chain_position` sub-fields at offsets 1
  and 2, overwriting reserved octets, and omitted `chain_exec_delay`
  entirely. Neither invented field is needed: a chain is defined
  positionally by consecutive members of one AVTPDU.
- **Clear-single (0x07)**: `clear_transaction_num` belongs at offset 3,
  not offset 1.

Two behavioral rules in the same area were also wrong rather than merely
missing: the chained `cs` bit is a *conditional start* selector, read on
the member about to run about its predecessor's outcome, but
`rcp_chained_advance()` read it off the member that errored -- inverting
who controls the abort. And compound's two sequencer sentinels
(`start_state == 0` meaning "start in any state", `next_state == 0`
meaning "remain in the current state") were unimplemented, so a
`start_state` of 0 never started and a `next_state` of 0 drove the
sequencer to state 0.

**Layer 2 -- none of it was reachable.** `rcp_compound_tick()`,
`rcp_compound_wait_tick()`, `rcp_triggered_tick()`,
`rcp_chained_advance()`, `rcp_timed_admit()` and `rcp_cancel_attempt()`
had, outside their own modules and unit tests, zero callers.
`src/mock.c`'s dispatcher never looked at `request_type` at all and
treated every ACF message identically; `rcp_sched_compare()`'s priority
ordering and `rcp_e2e_request_may_execute()`'s safety gate were both
individually correct, individually tested, and never invoked. Real
dispatch was unconditional FIFO, and only standard requests worked
end to end.

`server.h`/`server.c` therefore gains a per-endpoint **request store**
alongside the existing `ep_enable` pre-load queue, and with it the three
functions that join everything up: `rcp_server_endpoint_admit()`
(classify by `request_type` via `rcp_sched_classify()`, then route --
standard requests keep the original submit-or-queue path byte for byte,
conditional ones are decoded and stored, cancellations are reported for
the caller to apply), `rcp_server_endpoint_select_due()` (evaluate each
stored request through its own kind's predicate, gate safety-tagged ones
on `rcp_e2e_request_may_execute()`, and return the single highest-priority
due request per `rcp_sched_compare()`), and
`rcp_server_endpoint_complete()` (apply the completion action, then the
repetition rule). `mock.c` drives them: `rcp_mock_server_tick()`,
`rcp_mock_server_notify_trigger()`, `rcp_mock_server_set_sequencer_count()`,
`rcp_mock_server_watchdog_purge()`, chain sequencing across a frame's
members in `rcp_mock_server_dispatch_frame()`, and cancellation applied
against the store.

**Still open, deliberately out of scope**: `src/mock.c`'s dispatcher
still has no `ep_type` switch and never calls into any `ep_*.h` module's
own decode function for *standard* requests either -- it invokes only
the caller-supplied handler. This milestone makes conditional requests
reach dispatch correctly; making the reference server decode
endpoint-specific payloads itself is a separate, larger piece of work.

**Testing.** New `tests/test_conditional_dispatch.c` (22 cases) is the
first test in this repo to exercise these request kinds through the real
`rcp_mock_server_dispatch()`/`_tick()` path rather than each module's own
encode/decode in isolation -- it asserts on what the endpoint handler
actually saw and when, so a request kind that is stored but never
executes, executes with its condition unmet, or executes in the wrong
order fails the test. It caught one real bug during development (chained
arming restarted `chain_exec_delay` from the current tick instead of from
the predecessor's finalization). Each affected module also gains literal
octet-offset assertions written from the specification's figures rather
than copied back out of the encoder -- the pre-existing round-trip tests
all passed against the *wrong* layouts, since encode and decode agreed
with each other. Full `ctest` 59/59 passing.

`.fusa-reqs.json` gains `REQ-CMP-025`, `REQ-TIMED-009`..`011`,
`REQ-CHAIN-011`..`012`, `REQ-SRV-004`..`014` and `REQ-MOCK-021`..`026`,
and rewrites every requirement that described an old wire layout
(`REQ-CMP-010`/`015`/`019`..`023`, `REQ-TRIG-004`/`007`/`009`..`013`,
`REQ-TIMED-003`/`005`/`006`, `REQ-CHAIN-002`..`004`/`007`..`010`,
`REQ-CANCEL-005`/`007`) with the specification's own offsets and widths
cited. `include/rcp/version.h` had also drifted from `CMakeLists.txt`
(`0.100.0` vs `0.101.0`); both, and `.fusa.json`, are now `0.102.0`.

### 103. Endpoint conformance batch: PWM/GPIO subtract order, PWM EP_func register writes, ADC multi-value responses, discovery `svr_version` width, LIN/SPI `op` direction (v0.103.0) ✅

**BREAKING (wire format, and API).** Six independently-confirmed defects
across five endpoint modules, each re-verified against the
specification's own normative text before being changed. They are
batched because they are unrelated, one-module-each fixes that do not
overlap.

**1. The `evt[2:0] = 110b` subtraction was computed backwards
(`ep_pwm.c`, `ep_gpio.c`).** The single table row defining this
operation covers the GPIO and PWM_OUT endpoint group jointly and states
it as *payload minus current interface status*; both modules computed
*current minus payload*. Subtraction is not commutative, so every such
request produced a value a conforming peer would not -- and with the
same section's saturate-at-0x0000 rule, frequently produced 0 where a
real value was due. GPIO was corrected on that same normative sentence,
not by analogy to PWM: the row has no GPIO-specific worked example, and
its only parenthetical (about decreasing a PWM duty cycle) is an
illustrative note rather than a competing definition of the operand
order.

**2. `evt[2:0] = 111b` was a single enable-toggle bit; it is an addressed
EP_func register write (`ep_pwm.c`).** The configuration escape hatch is
defined as a 16-bit big-endian relative start address followed by
configuration data written into the endpoint's own functional-config
block. `ep_pwm.h` now models that block at its specified offsets and
widths -- EP_LEN, reserved, enable&clr, options, base clock, status,
clock divider, output signal flags, duty-cycle min/max, skew --
and `rcp_ep_pwm_out_apply_reconfig()` performs a real addressed write
over a rendered image of it, so a write spanning several registers, or
covering only one octet of a 16-bit register, lands correctly. The
"a payload whose length plus start address exceeds EP_LEN is to be
ignored" rule drops the whole write; a write covering a read-only
register leaves that register alone while the rest of the span still
applies. `rcp_ep_pwm_out_render_registers()` and
`rcp_ep_pwm_out_encode_reconfig_request()` complete the pair.
`rcp_ep_pwm_out_functional_cfg_t` loses its duplicate `enabled` bool --
the endpoint's enable bit is the EP-common one `regmap.h` already
models, which is exactly what the block's enable&clr register carries.

Two editorial defects in the source register table are documented in
`ep_pwm.h` rather than silently resolved: its EP_LEN description quotes a
length shorter than the extent of the table itself (the table's assigned
offsets are taken as authoritative), and it assigns two distinct 1-bit
parameters to the same bit position (the three signal flags are packed at
bits 0/1/2 in row order, the only self-consistent reading).

**3. ADC `adc_combine_avg_values` was a mode enum; it is an output-value
COUNT (`ep_adc.c`).** The register table defines the field as the number
of output values to be combined into one response, and the request
section states a response carries as many measurement values as half the
request's `read_size` (its own worked example: `read_size` 16, eight
16-bit values back). This module had reinvented the field as a four-way
AVERAGE/MIN/MAX/LATEST reduction and hard-coded every response to exactly
one 2-octet value, discarding the rest. The response codec is now
genuinely N-value: `rcp_ep_adc_encode_response()`/`_decode_response()`
carry `value_count` values and report `2 * value_count` as `read_size`;
`rcp_ep_adc_collect_response_values()` replaces the reduction with an
ordered packing step that carries a timed-out interval's NO_SIGNAL
through in its own slot instead of averaging it away;
`rcp_ep_adc_response_value_count()` expresses the read_size
relationship; and `rcp_ep_adc_encode_read_request()` now carries
`read_size` at all -- it previously left it 0, so a conforming endpoint
had been asked for nothing.

**4. The ADC response timestamp came from the wrong end of the averaging
window (`ep_adc.c`).** The rule is the moment the *last* sample feeding
the *first* averaged value included in the response was captured.
`rcp_ep_adc_average_interval()` reported `samples[0].timestamp` -- the
interval's *first* sample, the opposite end, and wrong by a full
averaging interval for any interval longer than one sample. It now
reports the last sample that actually contributed to the mean (falling
back to the interval's last sample when every sample timed out, since
that is still when the window closed).

**5. Discovery's `svr_version` was modelled 16-bit; the general register
map defines it as 32-bit (`discovery.c`, `regmap.h`).** Every field after
it sat two octets early on the wire, so a conforming client misparsed
`vendor_id`, `device_id` *and* `svr_ep_count` out of every discovery
response this library sent -- the first thing any real TC18 client reads
from a server. `RCP_DISCOVERY_GENERAL_SLICE_LEN` goes 12 -> 14,
`rcp_regmap_general_t::svr_version` and
`rcp_discovery_result_t::svr_version` become `uint32_t`, and all four
encode/decode paths (unfragmented, fragmented, and both decoders) are
corrected.

**6. LIN and SPI both encoded the `op` direction inverted (`ep_lin.c`,
`ep_spi.c`).** The general request-handling rule gives `op=0` as the
read/payload-response-expected direction and `op=1` as the
write/no-data-response one; the LIN endpoint's own concept section states
its reply rule in exactly those terms, and the specification's worked SPI
example -- captioned "write 20 bytes and get a response with 10" --
carries `op=0` with a non-zero `read_size`. Both modules encoded `op=1`
on a request that expects data back, *and* rejected the correct `op=0` as
malformed, so interop failed in both directions at once. Both modules'
existing tests asserted the inverted direction as correct; they now
assert the literal wire bit with the normative text quoted.

**Testing.** Full `ctest` 59/59 passing. Every corrected assertion cites
its section/table/figure and quotes the normative sentence it rests on,
so the tests are anchored to the specification rather than re-derived
from this library's own encoder -- the pre-existing round-trip tests all
passed against the wrong behaviour precisely because encoder and decoder
agreed with each other. New coverage: the PWM_OUT EP_func register block
(per-register offsets rendered against the table, a multi-register span,
partial multi-octet writes, the EP_LEN overrun rule, read-only registers,
and a full encode/apply round trip), the ADC eight-measurement response
geometry plus an end-to-end samples -> averages -> packed response ->
timed-response-timestamp pipeline test, and a discovery general-slice
octet-layout test pinning each field to its cited absolute address so the
two-octet regression cannot return unnoticed.

`.fusa-reqs.json` rewrites every requirement that described one of these
behaviours as correct: `REQ-GPIO-011`,
`REQ-PWM-007`/`010`/`011`/`016`/`023`,
`REQ-ADC-001`/`005`..`012`/`014`/`022`/`023`/`025`/`027`..`030`,
`REQ-DISC-010`/`012`, `REQ-LINEP-016`/`018`, `REQ-SPI-026`/`027`.

**Still open, deliberately out of scope**: `ep_i2c.c`'s transfer request
has the same request-carries-`op=WRITE` shape LIN and SPI had and may
carry the same inversion. It was not in this pass's independently
verified set and is left for its own change rather than fixed by
pattern-matching. *(Closed by milestone 104 — the independent check found
a genuinely different defect, so pattern-matching would have produced the
wrong fix.)*

### 104. I2C transfer direction: `op` is a parameter, not a constant (v0.104.0) ✅

Resolves milestone 103's open I2C question by checking the specification
directly rather than by analogy. **`ep_i2c.c` does not have LIN's and
SPI's inverted-`op` defect.** It has a different one.

§12.9.1 defines `op=0` as the read/payload-response direction and `op=1`
as the write/no-payload one, and §11.3.2/§11.3.3 mirror that split on the
response side. But §13.7.7.3 says only that the I2C payload "is the I2C
payload including the address" and that "the endpoint is just
transparent", and Figure 29 — rendered from the PDF at 600 dpi, since the
text extraction cannot show an empty table cell — **leaves the `op` cell
blank**, while spelling the *I2C-bus-level* R/W bit out inside the
payload (`1 1 1 1 0 A10 A9 RW`). The direction bit that matters to the
bus is a payload bit; `op` is the separate RCP-level question of what
comes back. LIN and SPI are unconditionally response-bearing and their
sections pin `op=0` for them explicitly (§13.7.10.1, Figure 23), so a
constant is right for those two. I2C is half duplex and either-directional,
so no constant is right for it — and §13.7.4's "A read request with a
`byte_msg_payload` as well as a write request …" confirms a
payload-bearing read request is an ordinary shape, which is exactly what
an I2C read is.

The module hard-coded the write sense and rejected the read sense as
malformed, so an I2C read could be neither sent nor received; a read
request could not carry a `read_size` at all; and every response was
encoded `op=0`, so a write confirmation classified as a read response.
`rcp_ep_i2c_dir_t` now makes the direction an explicit parameter of the
request and response codecs, `read_size` rides read requests and is
rejected on write requests (that slot is a `segment_num` there), and the
request decoder reports the direction instead of rejecting half of them.
`adapt.c` gains `rcp.i2c.read_size`, matching UART's and ADC's existing
mappings.

`.fusa-reqs.json` rewrites `REQ-I2C-010`..`015` and adds `REQ-I2C-017`
(`rcp_ep_i2c_dir_valid()`) and `REQ-I2C-018` (encode-time validation).
Full `ctest` 59/59 passing.

### 105. TC18 requirements-corpus completeness: catalog the specification's normative surface, gaps included (v0.105.0) ✅

A spec-coverage-gap audit read TC18 §10 through §13.7.13 clause by
clause and checked every MUST/shall sentence, every normative table row
and every figure-defined layout against all 817 existing `.fusa-reqs.json`
entries. Around a hundred and thirty mandatory clauses had **no**
corresponding entry at all — not a wrong requirement, a missing one. The
catalog described what this codebase does; it could not describe what the
specification requires and this codebase does not do, so nothing
distinguished "c-RCP implements §12.5" from "nobody has read §12.5".

This milestone changes only `.fusa-reqs.json`, the `//cfusa:req` tag
blocks that trace it, and five new test files. **`src/` is untouched.**

158 entries were added (817 → 975). Each carries a `"tc18"` field citing
the exact section/table/figure and the line of the specification text it
was read from, and a `"status"` field: `"implemented"` (9),
`"partial"` (62) or `"not-implemented"` (87). The 9 implemented ones keep
`scope: "tc18"` and ASIL-B — behaviour this library provides that was
simply never written down. The other 149 get a new `scope: "tc18-gap"`,
level/asil `QM`, and text opening with `NOT IMPLEMENTED:` that states
what TC18 requires, what c-RCP does instead, and the consequence. A
tc18-gap entry is deliberately **not** part of the ASIL-B safety-case
basis: claiming ASIL-B integrity for absent behaviour would be the same
dishonesty the pass exists to remove, in the other direction.

Every added entry is traced by a `//cfusa:req` tag in its module's public
header and a `//cfusa:test` tag in one of `tests/test_tc18_gaps_regmap.c`,
`_server.c`, `_ep.c`, `_ep2.c`, `_e2e.c`. Gap-entry tests are
**deviation-pinning**: they assert the behaviour the code actually has
where it departs from the clause (for instance that
`rcp_ep_i2c_mode_valid(4)` is *false* although TC18 Table 46 defines
Ultra-fast mode at value 4), so the deviation is a tested fact and a
future fix must update the requirement rather than drift silently.

Coverage by TC18 area: §12.5 Goto Sleep/Standby (8), §12.7.5 RC Server
register map (15), §12.7.6 HW pin mapping (6), §12.7.7 request-stream
config (9), §12.7.8 EP_ID map (7), §12.7.9 response-stream config (7),
§12.3 lifecycle (16), §12.4 power/start-up (8), §13.6 E2E safe points
(12), and the per-endpoint functional-configuration and trigger tables of
§13.7.1–§13.7.13 (48).

One existing entry was corrected rather than added: `REQ-E2E-003` claimed
the CRC covers `avtp_timestamp` as "8 bytes, big-endian" while
`src/e2e.c` has always serialised a `uint32_t` and `tests/test_e2e.c` has
always asserted 4 octets — the AVTPDU field is 32 bits, so the
requirement text was wrong, not the code.

Not closed here, and explicitly not claimed as complete: §11.2.2–§11.4,
§12.8.2, §12.9, §13.2/§13.3/§13.5 and part of §13.7.8–§13.7.13 still
carry roughly a hundred further candidate gaps from the same audit.

### 106. `rsp` never set on responses; `lifecycle_field_writable` inversion; citation backfill (v0.106.0/v0.106.1) ✅

TC18 §11.3 states plainly that `rsp = 1b` identifies an ACF_ABB/ACF_GBB
message as a response — every `rcp_ep_*_encode_response()`/
`rcp_discovery_encode_response*()` function across all 13 endpoint
modules left `rsp` at its zero-initialised default instead, making every
response this library ever produced wire-indistinguishable from a
request by that bit. Fixed everywhere at once (v0.106.0), alongside a
second, unrelated real bug found in the same pass:
`rcp_lifecycle_field_writable()`'s `HW_UNCONFIGURED` handling for R/W*
fields was inverted (rejecting the one state that must accept them).
`REQ-ACF-022` and the affected lifecycle requirement corrected to match.

v0.106.1 backfilled the `"tc18"` citation-string field onto 23 requirements
(`REQ-WIREERR-001`, the `REQ-CMP-*`/`REQ-TRIG-*`/`REQ-CHAIN-*`/
`REQ-TIMED-*`/`REQ-CANCEL-*` families) that v0.105.0's completeness pass
had left uncited, and separately fixed `rcp_acf_classify_response()`
(same version number by oversight): it checked `err`/`op` before `evt`,
so it never recognised `evt == 0xF` (TC18 §11.3.1's Acknowledge marker) —
every real decoded Acknowledge was misclassified as a Write/Read/Error
response instead, since a decoded header's `op` is never the encode-only
`RCP_ACF_OP_NONE` sentinel the old code relied on. Fixed by checking
`evt` first, per the spec text; mutation-tested.

### 107. TC18 §13.5 Table 30 Row-2 enforcement: ADC pilot (v0.107.0) ✅

Table 30's `{ADC, PWM_IN, I²C, LIN, CAN, UART, ISELED, MDIO}` row allows
`evt[2:0] = 000b` for a plain request; every other value but the
config-write shape (`111b`) is reserved (`UNSUPPORTED_CMD`). Nothing in
this codebase enforced that rule for any of the eight endpoint types —
each decoder silently accepted any `evt[2:0]` value. Added the shared
primitive, `rcp_acf_evt_row2_is_plain()` (`acf.h`/`acf.c`, `REQ-ACF-023`),
and wired it into ADC's decode path as the pilot, with a new
`RCP_EP_ADC_ERR_BAD_EVT` error code and mutation-tested tests.

### 108. TC18 §13.5 Table 30 Row-2 enforcement: I²C, UART, ISELED, MDIO (v0.108.0) ✅

Extends milestone 107's `rcp_acf_evt_row2_is_plain()` enforcement to the
four remaining simple Row-2 endpoint types (CAN needed its own,
larger fix — milestone 109 below). Each gains its own `_ERR_BAD_EVT`
variant on both its read and write decode paths where applicable, with
the same mutation-testing discipline.

### 109. CAN `FrameFormat` lives in the payload, not `evt[2:0]` (v0.109.0, BREAKING) ✅

The largest single defect this pass found. `ep_can.h` packed `FrameFormat`
(CBFF/CEFF/FBFF/FEFF/XL-classical/XL-new) into `evt[2:0]` as an invented
six-value selector — modelled on SPI's Table 30 row, which really does
use `evt[2:0]` as a selector, but CAN belongs to the shared Row-2 rule
instead, where those values must be *rejected*, not interpreted. TC18's
own Figure 39 ("can request format") places `FrameFormat` in the top 3
bits of the payload's leading quadlet instead (arbitration_id in the
low 29 bits) — pixel-verified at 400dpi against the rendered
specification page, not taken from text extraction alone.

Every CAN frame this module ever produced was wire-incompatible with a
real TC18 peer. Fixed comprehensively: `write_prefix()`/`read_prefix()`
now pack/unpack `FrameFormat` from the leading quadlet; every
`hdr.evt = frame_format` site changed to `hdr.evt = 0`; decode now calls
`rcp_acf_evt_row2_is_plain()` first, then validates `FrameFormat` from
the payload; `decode_frame_response_fragment()` lost its
per-fragment `out_frame_format` parameter (undeterminable once
`FrameFormat` moved out of `evt` — a fragment's own quadlet isn't
necessarily the leading one), moved instead to
`decode_reassembled_frame_response()` as a derived output. New error
codes `RCP_EP_CAN_ERR_BAD_EVT`/`RCP_EP_CAN_ERR_BAD_ARBITRATION_ID`. The
fix also uncovered and corrected `REQ-CANEP-031`, which had formally
catalogued the wrong evt-carries-FrameFormat design as `"implemented"`,
and a pre-existing test (`test_tc18_gaps_ep2.c`) that had baked the same
wrong assumption into a golden vector. Golden vectors hand-derived
independently (`(1<<29)|0x01ABCDEF = 0x21ABCDEF`, not copied from the
encoder under test); verified clean under ASan/UBSan locally before push.

### 110. TC18 §13.5.1 compound-wait `evt[2:0]` comparison primitive (v0.110.0) ✅

A compound-wait request's `evt[2:0]` carries a completely different
meaning than Table 30 gives every other request: it selects one of eight
ways to compare that request's own `byte_msg_payload` against the
addressed endpoint's current status — identically across every endpoint
type, unlike Table 30's per-row rules. Nothing in this codebase
implemented it. Two existing helpers this milestone supersedes,
`rcp_ep_spi_compound_wait_status_equal()` and
`rcp_ep_pwm_in_compound_wait_compare()`, were each only partial (a
handful of the eight modes) and, per milestone 111 below, were never
reachable from real request decode at all.

Added `rcp_acf_compound_wait_evt_valid()`/`rcp_acf_compound_wait_match()`
(`acf.h`/`acf.c`): exact match (`000b`), AND-with-1s-mask/AND-with-0s-mask
(`001b`/`010b`), reserved (`011b`), and four leading-quadlet high/low-word
`>=`/`<=` comparisons (`100b`–`111b`), including the length-capping rule
the specification states using its own SPI example ("only the first four
out of 20 received bytes will be checked when byte_msg_payload has only
four bytes"). Seven new requirements, `REQ-ACF-024`–`030`, each
independently hand-derived and pixel-verified against the rendered
specification page, individually mutation-tested (six targeted
mutations, one per mode/guard, each caught by exactly one failing test).

### 111. Wire the §13.5.1 primitive into real dispatch (v0.111.0, BREAKING) ✅

Milestone 110 added the comparison rule but left it unreachable:
`rcp_compound_encode_request()` had no way to set `evt` (every
compound-wait request silently encoded `evt = 0`, forcing exact-match
regardless of caller intent) and `rcp_compound_decode_request()`
discarded the decoded `evt` entirely. Both now thread it through.

Wiring this up surfaced a second, independent defect:
`rcp_server_tick_ctx_t`'s single `wait_condition_met` bool cannot
represent more than one pending `COMPOUND_WAIT` request per endpoint —
two requests pending on the same endpoint with different comparison
targets would have whichever single caller-computed result applied to
both indiscriminately. Replaced with `current_status`/`current_status_len`
(endpoint-scoped, caller-supplied) plus per-slot storage of each
request's own `evt`/`byte_msg_payload`
(`rcp_server_pending_t.compound_wait_evt`/`compound_wait_target`),
evaluated independently at every tick. Reserved `evt = 011b` now
rejected at admission (`RCP_SERVER_ADMIT_REJECTED`) per TC18's own
UNSUPPORTED_CMD rule, rather than stored as a request that could never
match. `rcp_ep_spi_compound_wait_status_equal()`/
`RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN` removed: both hardcoded an
SPI-specific fixed 4-byte comparison length that was never a real rule —
the specification's own length-capping rule is universal and driven by
`byte_msg_payload`'s actual length; its 4-of-20-bytes example merely
illustrates the general rule using SPI. New tests cover two
simultaneously-pending `COMPOUND_WAIT` requests with distinct targets
executing independently, and reserved-`evt` admission rejection, both
mutation-tested.

### 112. LIN's `evt[2:0]` comparison scheme was invented, not spec-derived (v0.112.0, BREAKING) ✅

`ep_lin.h`'s own file header admitted `rcp_ep_lin_compare_mode_t` (an
eight-value EXACT/PREFIX/ANY/NEVER+4-reserved enum) was "this module's
own original design... rather than on any spec-derived enumeration —
there being no such enumeration cited by the roadmap to derive one
from." There is one: TC18 §13.5 Table 30 places LIN in the exact same
`{ADC, PWM_IN, I²C, LIN, CAN, UART, ISELED, MDIO}` plain-request row
every other endpoint type's decoder already enforces via
`rcp_acf_evt_row2_is_plain()` — LIN is not called out as an exception
anywhere in it, pixel-verified. §13.7.10.1's own prose ("the LIN
endpoint checks each received message against the byte_msg_payload and
if a match under the conditions given by `evt[2:0]` is found a reply is
sent") describes the same universal §13.5.1 vocabulary milestone 110
built, not a LIN-private multi-mode scheme: since Table 30 constrains a
plain LIN request's `evt[2:0]` to `000b`, the only comparison that can
ever apply is §13.5.1 mode `000b`, exact match.

Removed the invented enum entirely. `rcp_ep_lin_encode_command_request()`
always encodes `evt = 0`; `rcp_ep_lin_decode_command_request()` rejects
`evt[2:0] != 000b` with the new `RCP_EP_LIN_ERR_BAD_EVT`. New
`rcp_ep_lin_response_matches()` delegates to `acf.h`'s shared
`rcp_acf_compound_wait_match(0, ...)` rather than reimplementing
exact-match logic of its own. `adapt.c`'s `RCP_ADAPT_OP_LIN_COMMAND`
mapping drops the now-meaningless `rcp.lin.compare_mode` metadata key.

Resolved `REQ-UART-035` (tracked `not-implemented`) as a side effect:
the milestone-110 primitive already covers UART's own compound-wait
comparison uniformly with every other endpoint type via `server.c`'s
`current_status` — its stale deviation-pinning test (which had used the
now-removed LIN helper as a workaround illustration) replaced with a
real conformance test.

`REQ-LINEP-001`–`005` (the invented enum) removed; `016`–`018` corrected;
`025`–`027` added. 984 requirements, 100% traced+tested throughout.

### 113. Close the per-function requirement-coverage gap (v0.113.0) ✅

A direct question -- does every public function carry at least one
traced requirement? -- exposed a bar `cfusa check`/`trace` cannot itself
verify: `trace`'s 100% gate only confirms every *catalogued* requirement
has impl+test tags, not that every function has a catalogued requirement
in the first place. A public-API cross-reference script (declared in
`include/rcp/*.h`, tagged in `src/*.c`, excluding internal `static`
helpers) found 39 genuine gaps, all in satellite/infrastructure modules
-- the TC18 protocol core was already fully covered. Tagged all 39,
confirmed each already had real test exercise, and gave two previously-
untested behaviors found along the way (`rcp_authz_policy_retain()`'s
refcounting, `rcp_in_memory_sink_spans()`'s cap-truncation) dedicated,
mutation-tested tests rather than just a tag.

Verifying with the correct CI-pinned `c-FuSa v0.5.50` binary (the cached
local one was stale, several commits behind the pinned tag) surfaced a
second, independent gap: 3 more untraced requirements
(`REQ-MDNS-007`/`008`, `REQ-RELAY-013`) the stale binary's `check`/
`trace` had silently passed, plus a `--func-coverage` naming-convention
gap (`tests/l2_veth_roundtrip.c` renamed to
`tests/l2_veth_roundtrip_test.c`, CMake target/CI job unchanged) so the
tool's test-file exemption -- which only recognizes the `test_*`/
`*_test.c` patterns every other test file in this repo already uses --
correctly excludes its `main()`.

984 → 1023 requirements, 100% traced+tested, 0 `cfusa check` errors.

### 114. Remove `rcp_acf_hdr_ack_has_event()`, not TC18-conformant (v0.114.0, BREAKING) ✅

Investigating this function's own TC18 basis (flagged during milestone
99's audit, deferred) found it describes a distinction TC18 does not
make. Its doc comment claimed to distinguish "a plain Acknowledge from
one tagged with an asynchronous event via `evt`," but TC18's own
`evt[3:0]` field table (§11.3) defines exactly one Acknowledge encoding
-- `0xF` -- with `0x0` = simple/data/error response, `0x1`-`0x8` = a
repetitive-response counter, and `0x9`-`0xE` reserved; none of those
apply once `evt[3:0] == 0xF` has already matched. Since
`rcp_acf_classify_response()` only reaches `RCP_ACF_RESP_ACKNOWLEDGE` via
`evt == 0xF`, `evt != 0` was always true for every real decoded
Acknowledge -- the function could only return `false` through a
hand-constructed header exercising the same `op == RCP_ACF_OP_NONE`
fallback `rcp_acf_classify_response()`'s own doc comment already
documents as unreachable from real decoded input, which is exactly what
all three of its tests did.

Removed the function, `REQ-ACF-003`, and its tests; corrected
`rcp_acf_classify_response()`'s and the `evt` field's doc comments to
stop pointing at it. No internal caller ever used it. 1023 → 1022
requirements, 100% traced+tested, 0 `cfusa check` errors.

### 115. GPIO write requests never respected input-pin configuration (v0.115.0)

Investigating cpp-RCP's identical write-semantics bug (issue #105 there,
fixed the same day) for a possible cross-repo instance found the same gap
here: TC18 §13.7.4.3 states "a write request to an input pin is ignored
for this input pin," but `rcp_ep_gpio_apply_write()` -- the only
write-application primitive this library exposes for GPIO -- has no
notion of per-pin direction at all, and this codebase never wrapped it
with masking anywhere; the function isn't even wired into any dispatch
path here (its only callers are its own 12 unit tests), so this was a
silent gap in the public API surface itself, not a wired-in behavioral
bug reachable through a request/response round trip today.

Added `rcp_ep_gpio_apply_masked_write()`: computes the same combined
value `rcp_ep_gpio_apply_write()` would, then commits it only for bit
positions whose `pins[i]` has `RCP_REGMAP_PIN_PROP_OUTPUT` set (the same
`pins[]`/`RCP_REGMAP_PIN_PROP_*` convention `rcp_ep_gpio_apply_reconfig()`
already established), leaving every input-configured bit unchanged
regardless of what the request or combinator result specify for it --
correct for every write semantics including a bare Replace, and a no-op
for `RCP_EP_GPIO_WRITE_RECONFIG` (already left unchanged by
`rcp_ep_gpio_apply_write()` itself). The existing
`rcp_ep_gpio_apply_write()` is untouched, including its own tests, which
correctly exercise the unmasked combinator in isolation -- this is a new,
additive function, not a rewrite of the old one.

`REQ-GPIO-037` added -- 1023 requirements, 100% traced+tested, 0 `cfusa
check` errors. Purely additive; non-breaking.

### 116. SHOULD/MAY audit: every RFC2119 SHOULD/MAY in TC18, extracted and referenced (v0.116.0)

Milestone 105's "TC18 requirements-corpus completeness pass" catalogued
the specification's MUST/SHALL surface; a direct question -- was
SHOULD/MAY also extracted, even where not implemented? -- found that it
had not been done as its own explicit pass. Closed the gap: grepped the
full spec text for `\bshould\b` (12 hits) and `\bmay\b` (44 hits),
excluded 6 legal-boilerplate hits, and individually read and classified
every one of the remaining 51 lines against this codebase.

Result: 10 SHOULD lines are all non-testable (six intro §2 design-goal
philosophy statements about the standard itself, three client-config-
authoring/request-composition advice pieces the RC Server cannot enforce,
one hardware register-calibration guideline). 41 MAY lines: 12 already
implemented and verified against actual code (Triggered/Timed requests,
gPTP sync, all three lifecycle states, power modes, Safety_Measure-driven
safe state, multi-request-per-frame dispatch, reduced sequencer counts,
the EP_USED bit, SPI's six channels, ADC's samples-per-interval
R/W choice, integrated-PHY-via-MDIO); one (Edge Node PTP/MACsec) is an
L1/L2 topology concern outside this library's RCP-wire scope entirely;
the remaining 28 are descriptive prose restating an already-implemented
architectural fact from a different angle, a client/hardware-deployment
choice outside what a protocol library tests, or a non-closed-list
permission ("this may be extended").

None of the 51 required a *new* `.fusa-reqs.json` entry -- every
implemented MAY-described capability already had an existing REQ id --
but per the direct follow-up instruction to ensure every SHOULD/MAY is
formally *referenced*, not just verbally accounted for: added a `tc18`
citation for the specific SHOULD/MAY line to the 10 existing REQ entries
covering an already-implemented optional capability (`REQ-TRIG-001`,
`REQ-TIMED-008`, `REQ-LIFECYCLE-001`, `REQ-LIFECYCLE-004`,
`REQ-PWRMODE-004`, `REQ-E2E-016`, `REQ-MOCK-019`, `REQ-SEQ-001`,
`REQ-MDIO-001`, `REQ-ADC-018`; `REQ-SPI-033` already had one). The
remaining 41 non-testable lines (all 10 SHOULD, 31 MAY) are recorded in
`docs/TC18-NON-NORMATIVE-CLAUSES.md`, each individually cited and
disposed -- design-goal prose about the standard itself, client/config-
authoring advice the server cannot enforce, hardware-deployment/physical-
layer choices outside this library's scope, or non-closed-list
permissions -- so every SHOULD/MAY occurrence in TC18 is findable from
this repo by citation, whether or not it maps to a testable requirement.

No code behavior changed; 1023 requirements (10 gained a citation, none
added or removed), 100% traced+tested, 0 `cfusa check` errors.

### 117. First real TC18 §12.9.6 error response: compound-wait's reserved evt (v0.117.0, BREAKING)

Part of a wider audit (github.com/SoundMatt/c-RCP/issues/163) that found
this library never actually constructed a wire-level Error Response
anywhere -- not in the core library, not in `mock.c`'s own reference
integration. `errors.h`'s `rcp_wire_error_t` enum (all 17 TC18 Table 27
codes) and `rcp_e2e_wire_error()` (the CRC/E2E mapping) only ever produced
a *value*; nothing consumed that value to build and return real response
bytes. `server.c`'s own comment at the compound-wait reserved-evt
rejection site already quoted TC18 §13.5.1's requirement ("an err-response
with error code = UNSUPPORTED_CMD shall be sent") right next to code that
never sent one.

Most of `rcp_server_endpoint_admit()`'s eight `RCP_SERVER_ADMIT_REJECTED`
paths reject *before* the request's own `byte_bus_id`/`transaction_num`
are decoded -- TC18 §12.9.6 requires an error response to carry both, so
nothing conformant can be built for those paths without a larger
restructuring (tracked in issue #163, not attempted here). Exactly one
rejection path already has everything decoded: a compound-wait request
whose `evt[2:0]` is the reserved `011b` value. This milestone wires that
one path, and the general-purpose primitive everything else in #163 will
reuse:

- `rcp_acf_build_error_response()` (new, `acf.h`/`acf.c`): builds a
  complete ACF_ABB error response -- given `byte_bus_id`, `transaction_num`,
  and an `rcp_wire_error_t` code, sets `err=1`, `rsp=1`, `evt=0` (any
  non-Acknowledge evt classifies the same way once `err` is set), and a
  one-octet `byte_msg_payload` carrying the code, per TC18 §12.9.6 and
  §11.3.4.
- `rcp_server_endpoint_admit()` gains a new `rcp_wire_error_t *out_error`
  output parameter (BREAKING: every caller must update its call site --
  two call sites in this repo, `mock.c` and the test suite). Always
  `RCP_ERROR_NONE` except the one now-determined case, which writes
  `RCP_ERROR_UNSUPPORTED_CMD`.
- `mock.c`'s `rcp_mock_server_dispatch()` -- this library's own reference
  integration -- now actually builds and returns that response
  (`finish_admission()` calls `rcp_acf_build_error_response()`, reading
  `transaction_num` back out of the request frame via
  `rcp_acf_unpack_header()` since `byte_bus_id` is already known from
  routing) instead of leaving `out_response` zeroed.

The other seven rejection paths, and 15 of the 16 still-unmapped Table 27
codes, remain open -- tracked in issue #163, not silently left unlabelled.

`.fusa-reqs.json` gains `REQ-ACF-031` (the new primitive) and
`REQ-SRV-022` (the new output parameter's one real case), both cited and
tested, including a mutation test (temporarily suppressed the fix,
confirmed the new end-to-end test fails, restored it). 1025 requirements,
100% traced+tested, 0 `cfusa check` errors. Full build + test suite +
ASan/UBSan all pass.

### 118. Second real error response: clear-single's REQUEST_NOT_FOUND (v0.118.0)

Issue #163 batch A. TC18 §11.2.3.3: "The request initiating the
cancellation will create an error response with the error code =
REQUEST_NOT_FOUND, when the clear_transaction_num was not found."
`rcp_server_endpoint_cancel_single()` already reported
`RCP_CANCEL_RESULT_NOT_FOUND` -- `mock.c`'s `apply_cancellation()`
discarded it (`(void)rcp_server_endpoint_cancel_single(...)`). Now builds
and returns a real error response via `rcp_acf_build_error_response()`,
carrying the *cancellation request's own* `byte_bus_id`/`transaction_num`
(TC18 §12.9.6's general rule) -- not the not-found target's, which this
milestone's new test specifically pins (`hdr.transaction_num == 93`, the
cancel request's own tn, not `91`, the not-found target).

Explicitly scoped narrower than clear-all/clear-non-safestate's own
`REQUEST_CANCELED` obligation (TC18 §11.2.3: every request a cancellation
*does* remove gets its own error response) -- that is a multi-response
fanout the current one-response-per-dispatch-call API shape cannot
represent, and is tracked separately, not attempted here.

`.fusa-reqs.json` gains `REQ-MOCK-028`, cited and tested, including a
mutation test (temporarily suppressed the fix, confirmed the new
end-to-end test fails, restored it). 1026 requirements, 100%
traced+tested, 0 `cfusa check` errors. Full build + test suite +
ASan/UBSan all pass.

### 119. Third and fourth real error responses: chained CHAIN_ERROR/CHAIN_ABORTED (v0.119.0)

Issue #163 batch D. TC18 §11.2.2.4: a chained request with no predecessor
(the first member in an AVTPDU is itself a chain request) sends
`CHAIN_ERROR` "to each request" in the broken chain; `cs=1` after a
predecessor errored sends `CHAIN_ABORTED`. `rcp_chained_advance()`
already correctly classified both outcomes -- `mock.c`'s
`rcp_mock_server_dispatch_frame()` discarded the response
(`memset(&out->response, 0, ...)`) in both branches.

Unlike batch A/B's clear-single case, this needed no new output
parameter: `is_chained_member()` already decodes each member's own
`transaction_num` internally (to validate it decodes as a real chained
request at all) but didn't expose it -- now does, alongside its existing
`cs` output. `byte_bus_id` was already available from the frame-splitting
loop. Each affected member gets its **own** independent response
(`rcp_mock_frame_member_result_t`'s per-member array already supports
this -- no multi-response fanout problem here, unlike batch B's still-open
`REQUEST_CANCELED`), which the new test specifically pins: two members in
one broken chain get two different responses, each carrying its own
`transaction_num` and its own error code.

Also fixed in passing: the pre-existing
`test_chained_first_in_frame_is_chain_error` test never freed
`results[0].response`/`results[1].response` -- harmless before this
milestone (both were always `{NULL,0}`), a real leak after it. Caught by
running the full ASan/UBSan suite as part of this milestone's own
verification, not by accident.

`.fusa-reqs.json` gains `REQ-MOCK-029`, cited and tested (one entry
covering both codes -- same wiring pattern, same function, same
citation-adjacent spec passage), including a mutation test. 1027
requirements, 100% traced+tested, 0 `cfusa check` errors. Full build +
test suite + ASan/UBSan all pass.

### 120. Fifth real error response: EP_NOT_FOUND for an unregistered byte_bus_id (v0.120.0)

Issue #163 batch E, first half. TC18 Table 27's `EP_NOT_FOUND` (8) covers
two distinct triggers: an addressed endpoint that doesn't exist, and a
Trigger request whose `trigger_source_ep` names a nonexistent endpoint.
This milestone wires the first (`rcp_mock_server_dispatch()`'s
`find_slot()` returning NULL) -- `byte_bus_id` is already a real, decoded
value (the function's own parameter) even when it names nothing
registered, and `transaction_num` is read back out of the request's own
header via `rcp_acf_unpack_header()`, the same technique batch A/B's
`finish_admission()` already established. `rcp_mock_server_dispatch_frame()`
gets this for free, since its per-member loop already delegates to
`rcp_mock_server_dispatch()` with the member's own `&out->response`.

The second trigger (`trigger_source_ep` validation) is NOT covered here
-- no code path anywhere validates a Triggered request's
`trigger_source_ep` against the server's registered-endpoint set at all;
that's real, new feature work (an admission-time or store-time endpoint-
registry lookup that doesn't exist yet), not a rewiring of an existing
correct decision like every fix in this milestone's batch so far. Tracked
separately on issue #163, not attempted here.

`.fusa-reqs.json` gains `REQ-MOCK-030`, cited and tested, including a
mutation test. 1028 requirements, 100% traced+tested, 0 `cfusa check`
errors. Full build + test suite + ASan/UBSan all pass.

### 121. Citation backfill, batch 1: AVTP (issue #164) (v0.121.0)

First batch of the citation-backfill effort (issue #164), started after
Phase 1's mechanical-wins sweep concluded. AVTP was 0/20 cited despite
being spot-checked earlier as legitimate, complete content -- purely a
citation gap. Cited 11 of 20 (`REQ-AVTP-001` through `009`, `013`, `014`)
against TC18 §11.1's NTSCF/TSCF header definitions (Figures 5/6) and the
worked wire-trace examples later in the spec that give the concrete
subtype byte values (0x82/0x05) the normative figures' own images don't
survive PDF text extraction for. The remaining 9 (`REQ-AVTP-010`-`012`,
`015`-`020`: stream_id/address equality semantics, transport refcounting,
loopback transport plumbing) are deliberately left uncited -- genuine
implementation-detail/test-infrastructure requirements with no specific
TC18 clause behind them, not an oversight.

Purely additive (new `tc18` citation fields only); no code or test
changed. 1028 requirements (unchanged count), 100% traced+tested, 0
`cfusa check` errors.

### 122. Citation backfill, batch 2: ACF (issue #164) (v0.122.0)

Second batch. Cited 11 of the 14 remaining uncited ACF requirements
against TC18 §11.2.1 Figure 7 / Table 4 (the ABB `byte_message_info`
header definition -- ACF_GBB shares the same first 8 octets, adding only
the `message_timestamp` region) and the `acf_msg_type` values (0x0E ABB,
0x0D GBB) that distinguish the two forms.

Left uncited, deliberately, pending closer investigation rather than a
speculative citation: `REQ-ACF-001` (strerror uniqueness -- genuine
implementation detail, no TC18 clause), `REQ-ACF-011` (zero the
`message_timestamp` region when untimed -- no explicit TC18 text found
mandating this specific encode-time zeroing, though it's a defensible
safety choice), `REQ-ACF-012` (`RCP_ACF_MTV_UNCERTAIN` as a third `mtv`
state -- TC18's own `mtv` bit is binary per Figure 7/Table 4; this may be
conflating the ACF_GBB `mtv` bit with the AVTP TSCF header's separate
`tu` bit, or modeling something not yet identified -- needs its own
investigation before citing, not force-fit here).

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 123. Citation backfill, batch 3: RMAP (issue #164) (v0.123.0)

Third batch. RMAP was 47/69 cited going in; cited 15 of the 22 remaining
against three distinct TC18 sources: §12.3.1.1 (`REQ-RMAP-001`/`002`,
EP0 = the discovery `byte_bus_id` 0x00, TC18.txt L2163-2164); §13.3
Table 33 (`REQ-RMAP-003`, `svr_root_client_index` 0 meaning no root
client, TC18.txt L3987-3989); §12.9.1.1 (`REQ-RMAP-004`-`008`, the
compound-request options-group bundling rule, TC18.txt L2033-2036);
§12.3.1.2 (`REQ-RMAP-009`-`012`, root-client write access via EP0 and
per-endpoint owning-stream write access, TC18.txt L2182-2184); and §13.2
Table 23 (`REQ-RMAP-020`-`022`, the EP_ID_config table's required
ascending `Request_Stream_Index`/`BBID` ordering, TC18.txt L2979).

Left uncited, deliberately: `REQ-RMAP-013` (pin-property bitmask bits
pairwise distinct) and `REQ-RMAP-014`-`019` (string-helper non-NULL/
uniqueness guarantees, config-struct zero-init boilerplate) -- all
genuine implementation-detail requirements with no TC18 clause behind
them, not an oversight.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 124. Citation backfill, batch 4: LIFECYCLE (issue #164) (v0.124.0)

Fourth batch. LIFECYCLE was 18/37 cited going in; cited 17 of the 19
remaining against TC18 §12.3.1's three lifecycle-state subsections,
including the plausibility-check bullet lists (`REQ-LIFECYCLE-002`/
`003`/`005`-`009`), the demotion-to-HW_UNCONFIGURED write path
(`REQ-LIFECYCLE-010`/`011`), the exhaustive transition set implied by
§12.3.1.1's explicit HW_UNCONFIGURED->RCP_CONFIGURED rejection
(`REQ-LIFECYCLE-012`), the NTSCF/discovery acceptance rules
(`REQ-LIFECYCLE-014`-`016`), the general TSCF/time-sync drop rule
already used for `REQ-AVTP-014` (`REQ-LIFECYCLE-017`), and the
HW_config/EP_generic_config lock plus the W*-marker convention
(`REQ-LIFECYCLE-018`-`020`).

Two things worth flagging from this batch, not silently:

- `REQ-LIFECYCLE-011`'s citation notes that TC18's Figure 16
  state-transition chart additionally gates the RCP_CONFIGURED ->
  HW_UNCONFIGURED demotion on all other EPs being idle (rejecting with
  `EPs_NOT_IDLE` otherwise, TC18.txt L2129-2131) -- c-RCP does not
  implement that gate. This is not a new finding: it's already tracked
  as its own gap requirement, `REQ-LIFECYCLE-022`
  (`scope: tc18-gap`, `status: not-implemented`), predating this batch.
  Cross-referenced rather than duplicated.
- `REQ-LIFECYCLE-017`'s citation likewise cross-references
  `REQ-LIFECYCLE-028`, the pre-existing gap entry for HW_CONFIGURED's
  TSCF-drop rule diverging from the code's general time-sync-conditional
  rule.

Left uncited, deliberately: `REQ-LIFECYCLE-013` (same-state transition
is a no-op) and `REQ-LIFECYCLE-021` (`rcp_lifecycle_strerror()` message
uniqueness) -- genuine implementation-detail requirements with no TC18
clause.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 125. Citation backfill, batch 5: PWRMODE (issue #164) (v0.125.0)

Fifth batch. PWRMODE was 16/28 cited going in; cited 10 of the 12
remaining against TC18 §12.4.1's cold-start/hot-start-up procedure
(`REQ-PWRMODE-003`, `005`-`010`, `012`) and §12.5's StandBy/Sleep entry
gating (`REQ-PWRMODE-013`).

Two of the new citations cross-reference pre-existing, already-tracked
gap requirements rather than citing over a real divergence:

- `REQ-PWRMODE-003` (cold-start target) cross-references
  `REQ-PWRMODE-014`: TC18 requires restoring the server's actually
  configured lifecycle state after a cold start, but
  `rcp_pwrmode_cold_start_lifecycle_target()` always returns
  `HW_UNCONFIGURED`. Predates this batch.
- `REQ-PWRMODE-005` (network-wake handshake bypass) cross-references
  `REQ-PWRMODE-020`: TC18 requires a network-woken server to skip only
  the interface-enable step and still complete the rest of the
  handshake, but `rcp_pwrmode_hotstart_required()` skips the entire
  handshake for a network wake. Predates this batch.

`REQ-PWRMODE-011` (`handshake_has_failed()`) is cited to the general
"repetition time... can be configured inside the WakeUp EP" text
(TC18.txt L2300-2301) with an explicit note that TC18 bounds repetition
by a configurable *time*, not a configurable *attempt count* --
`wakeup_repeat_limit` is this implementation's own finite-attempt
realization of that bound, not itself named in the spec text.

Left uncited, deliberately: `REQ-PWRMODE-001`/`002`
(`rcp_pwrmode_string()`/`strerror()` uniqueness) -- the same
implementation-detail pattern as prior batches' `*_strerror()`
requirements, no TC18 clause.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 126. Citation backfill, batch 6: PWM (issue #164) (v0.126.0)

Sixth batch, and the first not to complete a foundational/protocol-generic
module -- this is the first per-endpoint-type batch, chosen because PWM
was Phase 2's single largest remaining uncited category (54 of 58). Full
recount before this batch: 291/1028 cited overall (not the smaller
estimate carried in earlier session notes -- corrected here).

Cited 48 of the 54 uncited `REQ-PWM-*` requirements. PWM_OUT's
evt[2:0] write-modifier semantics (`REQ-PWM-001`-`009`: REPLACE/OR/AND/
XOR/reserved/ADD/SUB/RECONFIG-misroute) map directly onto §13.5 Table
30's GPIO/PWM_OUT row (TC18.txt L3690-3719), including the saturation
clause for ADD/SUB. Trigger semantics for both PWM_OUT (`REQ-PWM-012`-
`015`, Table 42) and PWM_IN (`REQ-PWM-032`-`034`, Table 44) map to
§13.7.5.1/§13.7.6.1. Lifecycle-gated writability for both endpoints'
functional config (`REQ-PWM-017`-`023`, `036`-`040`) reuses the same
§12.3.1.2/§12.3.1.3 basis already established for `REQ-LIFECYCLE-018`-
`020`. Request/response wire format (`REQ-PWM-025`-`031`, `042`-`046`)
cites §13.7.5.3/§13.7.6.3 plus the general §13.5 Table 30 frame-validation
basis already used for other endpoints' malformed-frame rejection reqs.
PWM_IN's compound-wait comparison modes (`REQ-PWM-048`-`053`) map to
§13.5.1's four numeric ≥/≤ evt[2:0] clauses (TC18.txt L3771-3782).

Left uncited, deliberately: `REQ-PWM-016`/`035` (functional-config
zero-init) and `REQ-PWM-024`/`041` (`strerror()` uniqueness) -- the same
implementation-detail pattern as prior batches. `REQ-PWM-047`/`054`
(the `RCP_EP_PWM_IN_NO_SIGNAL` 0xFFFF sentinel's round-trip and
compound-wait exclusion behavior) are also left uncited: TC18 defines
`PWM_IN_NO_SIGNAL` only as a Table 27 wire *error code* (value 9), not
as a payload sentinel value for PWM_IN's period/active_duration fields
-- the 0xFFFF-as-payload-sentinel convention is this implementation's
own design choice with no direct TC18 text behind it, the same kind of
genuine ambiguity already flagged for `REQ-ACF-012` in batch 2. Not
force-fit here either.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 127. Citation backfill, batch 7: GPIO (issue #164) (v0.127.0)

Seventh batch. GPIO was 5/37 cited going in -- the second-largest
uncited category after PWM, and the batch that motivated the previous
one's discovery: `REQ-GPIO-006` (the WRITE_REPLACE evt[2:0] semantics)
was seen uncited during batch 6's research and correctly predicted the
whole write-semantics cluster shares PWM_OUT's §13.5 Table 30 row.

Cited 30 of the 32 uncited `REQ-GPIO-*` requirements. GPIO's evt[2:0]
write-modifier semantics (`REQ-GPIO-005`-`012`) reuse the exact same
Table 30 GPIO/PWM_OUT row citations as batch 6's PWM_OUT half.
Lifecycle-gated functional-config writability (`REQ-GPIO-019`-`025`)
reuses the same §12.3.1.2/§12.3.1.3 basis already established for
LIFECYCLE and PWM. Request/response wire format (`REQ-GPIO-026`-`032`)
cites §13.7.4.1's GPIO-specific 4-byte/INVALID_PARAMETER rule
(TC18.txt L4391-4392) plus the general Table 30 frame-validation basis.
Pin/bitmask helpers (`REQ-GPIO-002`-`004`) cite §13.7.4.1's 32-pin/
bit-position description. Trigger semantics (`REQ-GPIO-014`-`017`) cite
Table 40.

Left uncited, deliberately: `REQ-GPIO-001` (`strerror()` uniqueness) and
`REQ-GPIO-018` (functional-config zero-init) -- the same
implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 128. Citation backfill, batch 8: SPI (issue #164) (v0.128.0)

Eighth batch. SPI was 5/35 cited going in. Unlike PWM_OUT and GPIO
(batches 6-7, which shared one Table 30 row), SPI has **its own
distinct Table 30 row**: evt[2:0]=000b-101b select one of 6
pre-configured channel settings and assert that channel's CSn, 110b is
reserved/UNSUPPORTED_CMD, 111b is the reconfiguration escape hatch --
a genuinely different semantic from GPIO/PWM_OUT's register-modify
row, read fresh from TC18.txt L3671-3693 rather than reused.

Cited 28 of the 30 uncited `REQ-SPI-*` requirements: channel-count and
CPOL/CPHA derivation (`REQ-SPI-002`-`005`) against §13.7.3.1's 6-channel
description and Table 39's `spi_clk_polarity0`/`spi_clk_phase0` fields
(the "SPI mode 0..3" grouping is this implementation's own convenience
layer over TC18's two independent bits, noted as such rather than
claimed as spec-named); trigger semantics (`REQ-SPI-006`-`009`) against
Table 38's per-channel CSn assert/de-assert events; lifecycle-gated
per-channel functional-config writability (`REQ-SPI-011`-`025`) reusing
the §12.3.1.2/§12.3.1.3 basis from prior batches, each paired with its
own Table 39 field citation where one exists; and transfer/response
wire format (`REQ-SPI-026`-`030`) against §13.7.3.3's read-direction
transfer semantics and Figure 23's worked "write 20 bytes and get 10
back" example.

Left uncited, deliberately: `REQ-SPI-001` (`strerror()` uniqueness) and
`REQ-SPI-010` (functional-config zero-init) -- the same
implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 129. Citation backfill, batch 9: UART (issue #164) (v0.129.0)

Ninth batch. UART was 8/37 cited going in. Unlike SPI/GPIO/PWM_OUT,
UART's evt[2:0] carries no per-value meaning of its own in TC18 §13.5
Table 30 -- it's grouped with ADC/PWM_IN/I2C/LIN/CAN/ISELED/MDIO in the
row where evt=000b-110b is reserved (ordinary requests simply carry
evt=0) and only evt=111b (reconfig) is defined, TC18.txt L3684-3693.

Cited 27 of the 29 uncited `REQ-UART-*` requirements against §13.7.8's
three subsections: bit-padding for non-multiple-of-8 frame widths
(`REQ-UART-001`-`003`), Table 48's functional-config register fields
paired with the §12.3.1.2/§12.3.1.3 lifecycle-writability basis already
established in prior batches (`REQ-UART-005`-`016`), and the general
Table 30 row plus §13.7.8.1's read-completion/fragmentation rules for
wire format (`REQ-UART-018`-`031`).

Two citations explicitly cross-reference pre-existing gap requirements
rather than implying full conformance: `REQ-UART-008`/`009`
(`set_baud_rate`) and `REQ-UART-015`/`016` (`set_timeout`) note that
their units diverge from Table 48's own (kbit/s, bit-times) -- already
tracked at `REQ-UART-037`, predating this batch. `REQ-UART-013`/`014`
(`set_rx_buffer_size`) cite TC18's prose description of the
fifo-rx-buffer rather than a Table 48 register, since TC18 never
assigns `uart_rx_fifo_size` a register address -- consistent with the
existing gap notes at `REQ-UART-032`/`036`.

Left uncited, deliberately: `REQ-UART-004` (functional-config zero-init)
and `REQ-UART-017` (`strerror()` uniqueness) -- the same
implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 130. Citation backfill, batch 10: DISC (issue #164) (v0.130.0)

Tenth batch, and the first not to be a per-endpoint-type module --
DISC is the RC-Server-level discovery protocol (§12.6), read fresh
rather than assumed to fit the endpoint-type pattern of batches 6-9.
DISC was 1/29 cited going in.

Cited 27 of the 28 uncited `REQ-DISC-*` requirements against §12.6.1
Table 16 (discovery request: fixed NTSCF/ACF_ABB/byte_bus_id=0/op=read
shape) and §12.6.2 Table 17 (discovery response: same shape, payload
carrying the register map from address 0x00000, length bounded by
read_size). The discovery-stream claim mechanism
(`REQ-DISC-015`-`022`: claim/release/timeout/preemption) cites §12.6's
own prose -- the first discovery request claims the discovery stream,
a `Discovery_TimeOut` with no configuration traffic releases it, and
only the claimant's configuration requests are accepted while held --
since TC18 describes this entirely in running text with no dedicated
table. The discovery result cache (`REQ-DISC-023`) cites §12.6.2's
"maintain a table of relevant discovered RC Servers" client-side
guidance.

Left uncited, deliberately: `REQ-DISC-024` (`strerror()` uniqueness) --
the same implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 131. Citation backfill, batch 11: ADC (issue #164) (v0.131.0)

Eleventh batch. ADC was 8/36 cited going in. Per-endpoint-type module,
same shape as PWM/GPIO/SPI/UART: §13.7.9's basic-concept, functional-
config (Table 51), and request-handling subsections, plus Table 50's
five trigger events and the general Table 30 row (ADC shares UART's
row: only evt=111b defined).

Cited 22 of the 28 uncited `REQ-ADC-*` requirements: averaging/
response-packing semantics (`REQ-ADC-001`-`002`, `005`-`008`, `010`-
`013`) against §13.7.9.1's averaging-interval and COMBINE_NR_VAL text
plus §13.7.9.3's "half the read size" and response-timestamp rules;
lifecycle-gated functional-config writability (`REQ-ADC-015`-`023`)
reusing the established §12.3.1.2/§12.3.1.3 basis, each paired with its
own Table 51 field citation where one exists; wire format (`REQ-ADC-
025`, `027`-`029`) against §13.7.9.3 and the general Table 30 row.

This batch's own citation script had a bug caught before merge: it
included `REQ-ADC-026` as a target even though that requirement was
already cited from earlier work. Because the script locates each
edit site by searching for the next *uncited* pattern after a
requirement's `id`, that already-cited entry caused every subsequent
edit in the batch to land one requirement too late -- `REQ-ADC-027`
through `030` each received the citation intended for the previous
one, and `030` was wrongly cited at all (it should have stayed
uncited, matching the same `RCP_EP_PWM_IN_NO_SIGNAL` sentinel ambiguity
already established for `REQ-PWM-047`/`054` in batch 6 -- TC18 defines
`PWM_IN_NO_SIGNAL` only as a Table 27 wire error code, not a payload
sentinel). Caught during this batch's own review, before any commit;
fixed by hand-correcting the four affected entries. All 9 previously
merged script-based batches (PWM/GPIO/SPI/UART/DISC) were audited
against their pre-batch `.fusa-reqs.json` state at their base commits
and confirmed clean -- none had a target already cited going in, so
none could have hit this failure mode. (RMAP/LIFECYCLE/PWRMODE used
direct per-entry edits, not this script pattern, and were never at
risk.)

Left uncited, deliberately: `REQ-ADC-003`/`004`/`009`/`030`
(`RCP_EP_PWM_IN_NO_SIGNAL` handling -- the sentinel ambiguity above),
`REQ-ADC-014` (functional-config zero-init), and `REQ-ADC-024`
(`strerror()` uniqueness) -- the same implementation-detail pattern as
every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 132. Citation backfill, batch 12: E2E (issue #164) (v0.132.0)

Twelfth batch, and the first PR to use the pre-flight safety check
added after batch 11's self-caught citation-script bug -- every target
requirement was verified to be in the module's true uncited set before
any edit ran, and the script printed "Pre-flight OK" confirming 26/26
targets before writing anything. E2E was 16/44 cited going in.

E2E spans two genuinely different TC18 regions: the CRC32 mechanism
itself (§13.6, `REQ-E2E-002`-`010`) against Table 31's CRC32P4
parameter set and the ABB/GBB CRC coverage/fragmentation rules, and
the safety/watchdog machinery (`REQ-E2E-011`-`027`, `043`-`044`) which
is NOT in §13.6 at all -- it's split between §11.2.2's safety-request
MSB convention (request_type 0x8x, Table 5) and §12.7.7 Table 22's
per-stream `rx_enforce_e2e`/`rx_wd_*`/`rx_safety_measure` register
block. Reading both regions fresh, rather than assuming everything
E2E-prefixed lives in §13.6, is what made this batch tractable.

Cited 26 of the 28 uncited `REQ-E2E-*` requirements. Left uncited,
deliberately: `REQ-E2E-001` (`strerror()` uniqueness) and
`REQ-E2E-006` (`rcp_e2e_wrap()` failing safe on invalid input or
allocation failure -- defensive-programming detail, no TC18 clause) --
the same implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 133. Citation backfill, batch 13: CANEP (issue #164) (v0.133.0)

Thirteenth batch. CANEP was 11/32 cited going in -- already
substantially seeded from earlier work (`REQ-CANEP-001`, `016`, `017`,
`020`, `026`-`032` all pre-cited to §13.7.11.3 Figure 39/Table 54 and
§13.7.11.2 Table 53), which made this batch's citation text mostly a
direct reuse of that already-established basis rather than fresh
spec forensics.

Cited 19 of the 21 uncited `REQ-CANEP-*` requirements: frame-format/
arbitration-id/data-length helpers (`REQ-CANEP-002`-`005`) against
Table 54 and §13.7.11.3's remote-frame/CAN-ID-alignment and CAN
XL 2054-byte payload text; the four-entry XL acceptance-filter table
(`REQ-CANEP-006`, `014`) against Table 53's four `acceptance filter`
rows; per-field functional-config setters (`REQ-CANEP-008`-`013`)
against their own Table 53 register plus the established
§12.3.1.2/§12.3.1.3 lifecycle-writability basis; frame request/response
round-trips (`REQ-CANEP-018`, `019`, `021`, `022`) against the same
Figure 39 basis already used for the sibling encode/decode
requirements; and fragmentation (`REQ-CANEP-023`-`025`) against
§13.7.11.3's "requires segmentation, which is supported by the fields
'ms' and 'segment_num'" text.

Used the pre-flight citation-target check added after batch 11 --
confirmed all 19 targets were genuinely uncited before writing anything.

Left uncited, deliberately: `REQ-CANEP-007` (functional-config
zero-init) and `REQ-CANEP-015` (`strerror()` uniqueness) -- the same
implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 134. Citation backfill, batch 14: ISELED (issue #164) (v0.134.0)

Fourteenth batch, first module whose own file header explicitly
documents that most of its behavior is this implementation's *own*
original design rather than TC18-specified content: `ep_iseled.h`
states plainly that the native 4-bit/5-bit bit-framing line code, the
CRC-8 (poly 0x07) algorithm, and the trigger-enable toggle are "this
module's own original design... not values taken from either
[ISELED's own] ecosystem's own specification or the confidential TC18
extraction" -- TC18 itself only says data is "4/5bit encoded according
to the ISLED standard" (§13.7.12.1) without giving the bit-level
scheme. Respecting that distinction meant citing substantially fewer
of ISELED's 23 uncited requirements than prior per-endpoint-type
batches, rather than force-fitting a TC18 citation onto genuinely
invented behavior.

Cited 9 of the 23 uncited `REQ-ISELED-*` requirements: recovered-clock
pin requirements (`REQ-ISELED-007`) against §13.7.12.2's Freq_Sync/
ISP_N prose; the transmission-complete trigger (`REQ-ISELED-008`)
against §13.7.12.1's "creates a single trigger event... when
transmission... has been completed" line; the R/W* functional-config
writability gate, clock-divider, and recovered-clock-mode setters
(`REQ-ISELED-010`-`012`) against Table 55's `iseled_ep_options`/
`iseled_clk_divider`/`iseled_use_rcv_clk` register rows plus the
established §12.3.1.3 W*-marker writability basis; the CRC-*enable*
gate itself (`REQ-ISELED-013`, distinct from the CRC-8 *algorithm* it
gates) against §13.7.12.1's "optional CRC generation... can be
enabled" prose; and the command/response wire encode/decode pair
(`REQ-ISELED-021`, `023`, `024`) against Figure 40/41's iseled request/
response formats.

Left uncited, deliberately, as genuinely this-module's-own-design with
no TC18 basis (not a citation-backfill gap): the symbol encode/decode,
bitframe length/encode/decode, and CRC-8 primitives (`REQ-ISELED-001`-
`006`), the bitframe decoder's own four error paths
(`REQ-ISELED-016`-`020`), and the trigger-select setter
(`REQ-ISELED-014`, no Table 55 or Table 32 register found for it --
TC18 defines ISELED as having exactly one trigger, not a selectable
one). Also left uncited per the standing pattern:
`REQ-ISELED-009` (zero-init) and `REQ-ISELED-015` (`strerror()`
uniqueness). `REQ-ISELED-022` was already cited; `REQ-ISELED-025`-`028`
are pre-existing `tc18-gap` entries, already cited.

Filed issue #184 to track two genuine open TC18 ambiguities surfaced
during this session's citation work (`REQ-ACF-012`'s `MTV_UNCERTAIN`,
batch 2; and the `RCP_EP_PWM_IN_NO_SIGNAL` payload-sentinel question
touching PWM_IN/ADC, batches 6/11) so they aren't lost once issue #164
closes.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 135. Citation backfill, batch 15: I2C (issue #164) (v0.135.0)

Fifteenth batch. §13.7.7 I²C Controller endpoint, 17/20 uncited going
in (`REQ-I2C-012`, `019` already cited -- `019` is the pre-existing
`tc18-gap` entry for Table 46's own duplicated "3:" high-speed-mode
labeling, cross-referenced rather than re-diagnosed).

Cited 15 of the 17 uncited `REQ-I2C-*` requirements: lifecycle-gated
functional-config writability (`REQ-I2C-003`-`005`) reusing the
established §12.3.1.2/.3 basis; the `i2c_mode` register validity/setter
trio (`REQ-I2C-001`, `006`-`008`) against Table 46, with `001`
explicitly cross-referencing `REQ-I2C-019`'s spec-ambiguity gap rather
than treating the conservative 0..3 cap as a fresh finding; the
transfer request/response encode/decode set (`REQ-I2C-010`, `011`,
`013`-`016`, `018`) against Figure 29's i2c request format plus
§13.7.7.3's "the I2C endpoint does not know whether there is a 7- or
10-bit address, since the endpoint is just transparent" passthrough
text; and the direction-validity primitive (`REQ-I2C-017`) against
§13.7.7.1's own description of I2C as the one endpoint type genuinely
accepting both read and write op senses on the same transfer, unlike
every fixed-op endpoint before it.

Used the pre-flight citation-target check -- confirmed all 15 targets
were genuinely uncited before writing anything.

Left uncited, deliberately: `REQ-I2C-002` (functional-config
zero-init) and `REQ-I2C-009` (`strerror()` uniqueness) -- the same
implementation-detail pattern as every prior batch.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 136. Citation backfill, batch 16: CMP (issue #164) (v0.136.0)

Sixteenth batch. Unlike ISELED (batch 14), this module's file header
carries the same "own original design" disclaimer for its *exact
byte-level packing* of the repurposed `message_timestamp` region --
but the underlying *behavior* that packing carries turned out to have
rich, well-defined TC18 basis: §11.2.2's conditional-request framework
(Table 5's request_type identifier table), §11.2.2.1's compound
request semantics (Table 6/Figure 8), and §11.2.2.2's compound-wait
semantics (Table 7/Figure 9) spell out `cmp_start_state`/
`cmp_next_state`/`cmp_exec_delay`/`cmp_repetitions` and their
`cmpw_*` counterparts in the same detail Table 30 gave PWM/GPIO/SPI's
evt[2:0] semantics in earlier batches -- this module's own code
comments already cited "extraction §3.14" for the advance-guard rule,
and that rule turned out to trace directly to §11.2.2.1's own prose.

Cited 16 of 16 uncited `REQ-CMP-*` requirements (the 17th, `004`
`strerror()`, stays uncited per the standing pattern; `001`-`003`,
`010`, `015`-`018`, `026`-`027` were already cited from earlier work):
the request-type dispatch/peek trio (`REQ-CMP-005`-`007`, `011`-`014`)
against §11.2.2's conditional-request framing and Table 5's opcode
table; the encode-time request_type/payload-length validation
(`REQ-CMP-008`-`009`) against Table 6/Figure 8 and Table 7/Figure 9's
wire formats; and the sequencer-advance behavioral core
(`REQ-CMP-019`-`025`: the advance-only-if-still-in-start_state guard,
the start_state=0 "any state" rule, exec_delay elapsing, and both
compound's unconditional and compound-wait's condition-gated tick
functions) against §11.2.2.1/§11.2.2.2's own prose describing exactly
that state machine.

Used the pre-flight citation-target check -- confirmed all 16 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 137. Citation backfill, batch 17: LINEP (issue #164) (v0.137.0)

Seventeenth batch. §13.7.10 LIN commander endpoint, 16/22 uncited
going in.

Cited 12 of the 16 uncited `REQ-LINEP-*` requirements: the single
transmission-done trigger (`REQ-LINEP-006`) against §13.7.10.1's "The
LIN EP issues a trigger when a transmission has been finalized, and
the configured trailing time has expired" line; lifecycle-gated
functional-config writability (`REQ-LINEP-008`-`010`) reusing the
established §12.3.1.2/.3 basis; the `lin_clk_divider` register
validity/setter (`REQ-LINEP-011`-`012`) against Table 52; and the
command/response encode/decode set (`REQ-LINEP-017`-`022`) against
Figure 38's lin request/response format plus §13.7.10.1's own reply
rule ("the LIN endpoint checks each received message against the
byte_msg_payload and if a match... is found a reply is sent if op =
0").

Left uncited, deliberately: `REQ-LINEP-007` (zero-init), `REQ-LINEP-015`
(`strerror()`), and the trigger-select setter (`REQ-LINEP-013`-`014`)
-- same no-register-basis situation as `REQ-ISELED-014` in batch 14:
`ep_lin.h`'s own header self-documents the selectable-trigger toggle
as "this endpoint's own analogue of ep_spi.h's TRANSFER_DONE trigger,"
i.e. this implementation's own design, not a Table 52/Table 32 wire
field.

Used the pre-flight citation-target check -- confirmed all 12 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 138. Citation backfill, batch 18: WAKEUP (issue #164) (v0.138.0)

Eighteenth batch. §13.7.2 Wakeup control, 16/22 uncited going in.
Before committing to this category, this batch's own scoping pass
first checked MDIO (16 uncited) and found its file header
self-documents essentially its *entire* wire layout as original,
non-TC18 design (already tracked as `tc18-gap` #021/#022) -- only one
requirement in it would have been genuinely citable. WAKEUP, by
contrast, has rich underlying TC18 prose (§13.7.2.1's basic concept,
Table 36/37's register block, §13.7.2.3's sleep-request handshake +
Figure 22 + the "SleepCMD is coded as 0xA5" line) despite the module
already carrying six pre-existing `tc18-gap` entries
(`REQ-WAKEUP-017`-`022`) documenting real, specific divergences from
that same prose.

Cited 13 of the 16 uncited `REQ-WAKEUP-*` requirements, several
deliberately cross-referencing those pre-existing gap entries rather
than implying full conformance: lifecycle-gated functional-config
writability (`REQ-WAKEUP-002`) reusing the established §12.3.1.2/.3
basis; wake-source assertion (`REQ-WAKEUP-003`-`004`) against
§13.7.2.1's activity-detection prose and Table 36/37, cross-referencing
`REQ-WAKEUP-021`/`022` for the module's reduced single-sense/
single-latch modeling versus Table 37's six-value IO_SRC encoding and
Table 36's per-source bitfield; the wup_status latch primitives
(`REQ-WAKEUP-005`-`008`) against Table 36's wup_status register text,
same `REQ-WAKEUP-021` cross-reference; the SleepCMD request/response
pair (`REQ-WAKEUP-010`-`013`) against Figure 22, the 0xA5 opcode, and
the sleep-request acknowledge/refusal prose, with the response pair
cross-referencing `REQ-WAKEUP-019`'s divergence (refusal signalled as
a positive-form response, not an error response); and the WakeUp
message pair (`REQ-WAKEUP-014`-`015`) against §13.7.2.1's repetitive-
message prose, cross-referencing `REQ-WAKEUP-017`'s divergence (no
wake-up source field carried).

Left uncited, deliberately: `REQ-WAKEUP-001` (zero-init),
`REQ-WAKEUP-009` (`strerror()`), and `REQ-WAKEUP-016` (the wakeup-echo
helper -- transaction_num correlation is this module's own
implementation convenience with no direct TC18 text of its own).

Used the pre-flight citation-target check -- confirmed all 13 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 139. Citation backfill, batch 19: SRV (issue #164) (v0.139.0)

Nineteenth batch. `server.c`/`server.h` -- the RC Server's per-endpoint
request-storage/admission/priority-scheduling/completion core, 15/22
uncited going in. Before committing, this batch's scoping pass first
checked PWR (`powerstate.c`, 15 uncited) and found it a thin
client-side convenience wrapper over already-cited primitives
(ep_wakeup.h + power.h) with no TC18 text of its own -- rejected as a
batch candidate, same reasoning as batch 18's MDIO rejection. SRV, by
contrast, sits at the heart of TC18's own request-handling model and
has the richest single-batch TC18 basis of the session: §12.9.1/
§12.9.1.1's request-handling and multi-request-per-frame admission
rules, §12.9.2's priority-in-execution ordering (cancellation >
triggered > timed > compound > compound-wait > chained > standard,
plus same-priority FIFO tiebreak), and §12.9.3 Table 26's per-
request-type execution procedure, which maps almost line for line onto
`rcp_server_endpoint_complete()`'s own per-kind tick/repeat_count
switch.

Cited all 15 of the 15 uncited `REQ-SRV-*` requirements -- SRV is now
100% cited. Highlights: the pre-load-then-drain-on-enable queue
(`REQ-SRV-001`-`003`) against Table 32's `ep_enable` common field;
multi-request admission/rejection (`REQ-SRV-004`-`005`) against
§12.9.1.1's per-request accept/reject rule and Table 5's request_type
identifiers; the full priority-selection function
(`REQ-SRV-006`-`008`) against §12.9.2's seven-tier ordering, the
safety-tagged persistence rule (cross-referencing the already-cited
`REQ-E2E-012`/`013` e2e.h gate this function calls), and the FIFO
tiebreak; completion (`REQ-SRV-009`-`010`) and trigger-occurrence
recording (`REQ-SRV-011`) against Table 26's compound/compound-wait/
triggered rows; chain-predecessor marking (`REQ-SRV-012`) against
§11.2.2.4's "the second and following requests always wait on the
predecessor request to finish its execution" rule; cancellation
(`REQ-SRV-013`) reusing Phase 1's already-established §11.2.3.1/.2
basis; watchdog purge (`REQ-SRV-014`) against the 0x0F-cleared/
0x8F-remains-active watch-dog-overflow rule already used for CMP; and
the pending-count getter (`REQ-SRV-021`) against §12.9.2's own "EP
request storage" framing.

One citation-text error caught and fixed during scoping (before any
edit was written): `rcp_e2e_request_may_execute()` was initially
attributed to `REQ-E2E-014` by proximity guess; a direct grep against
`src/e2e.c`'s own tags showed it is actually `REQ-E2E-012`/`013` --
corrected before the citation script ran. Always verify a
cross-reference's req-id against the real source tag, never infer it
from nearby line numbers.

Used the pre-flight citation-target check -- confirmed all 15 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 140. Citation backfill, batch 20: SCHED (issue #164) (v0.140.0)

Twentieth batch. `scheduler.c`/`scheduler.h`, 8/8 uncited (100%) going
in -- the first fully-uncited-to-fully-cited batch since ADC-class
work began, discovered directly from SRV's own research: the
scheduler module is the same §12.9.1.1/§12.9.2 machinery SRV already
consumes (`rcp_sched_compare()` is what `rcp_server_endpoint_select_
due()` calls), just exposed as its own small, independently-testable
unit.

Cited all 8 of the 8 uncited `REQ-SCHED-*` requirements: request-kind
classification/ranking/comparison (`REQ-SCHED-001`-`003`) against
§12.9.2's exact seven-tier priority list already cited for SRV plus
the same-priority FIFO tiebreak; multi-ACF-per-frame splitting
(`REQ-SCHED-004`-`006`) against §12.9.1.1's "An RCP frame may include
multiple ACF-types (requests). An RC Server shall support to handle
multiple requests in one frame" rule; and TSCF timing consistency
(`REQ-SCHED-007`-`008`) against §12.9.1.1's "A presentation time from
a TSCF header will be applied to all ACFtypes within the frame" rule.

Used the pre-flight citation-target check -- confirmed all 8 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 141. Citation backfill, batch 21: TRIG (issue #164) (v0.141.0)

Twenty-first batch, continuing the conditional-request vein SRV's
research opened. `request_triggered.c`/`request_triggered.h`, 9/13
uncited going in.

Cited 8 of the 9 uncited `REQ-TRIG-*` requirements: request encode/
decode validation (`REQ-TRIG-003`, `005`-`007`) against §11.2.2.3
Figure 10/Table 8's triggered-request wire format (request_type=0x0E/
0x8E, trigger_source_ep/trigger_signal_nr/trigger_threshold/
trigger_exec_delay/trigger_repetitions) and the shared §11.2.2
mtv=0-repurposing intro + Table 5 opcode identifiers; the
occurrence-counter start/record pair (`REQ-TRIG-008`-`009`) against
Table 26's Triggered-request row ("Upon entering RS state the counted
number of received trigger signals is reset to zero and newly
arriving trigger signals are counted from zero") and Table 8's
trigger_source_ep/trigger_signal_nr field definitions; and the fire
tick (`REQ-TRIG-012`-`013`) against the same Table 26 row's
trigger_exec_delay-expiry-plus-EP-idle gating text.

Left uncited, deliberately: `REQ-TRIG-002` (`strerror()` uniqueness)
-- the same implementation-detail pattern as every prior batch.

Used the pre-flight citation-target check -- confirmed all 8 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 142. Citation backfill, batch 22: CHAIN (issue #164) (v0.142.0)

Twenty-second batch. `request_chained.c`/`request_chained.h`, 9/12
uncited going in. One id-mapping error caught and fixed before
running the citation script this batch: initially drafted a citation
for `REQ-CHAIN-002`, but the live field-presence scan showed it was
already cited (`REQ-CHAIN-001`, the `strerror()` requirement, was the
one actually uncited and correctly left that way) -- corrected the
target list before writing anything, the pre-flight check's purpose
working exactly as intended.

Cited 8 of the 9 uncited `REQ-CHAIN-*` requirements: member encode
(`REQ-CHAIN-003`) and decode validation (`REQ-CHAIN-005`-`007`)
against §11.2.2.4's Figure 11 chained-request wire format
(request_type=0x01, reserved octets, chain_exec_delay, cs) and the
shared §11.2.2 mtv=0-repurposing intro + Table 5's `0x01` opcode; the
no-predecessor CHAIN_ERROR rule (`REQ-CHAIN-008`) against "If the
first request in an AVTPDU is a chain request, then there is no
predecessor to chain to, thus the entire chain will be ignored. An
error response with the error code 'CHAIN_ERROR'..."; the cs-bit-driven
CHAIN_ABORTED rule (`REQ-CHAIN-009`) against the cs field's own two-
value definition; exec-delay elapsing (`REQ-CHAIN-011`) against
chain_exec_delay's own field description; and the reserved-octet
rejection rule (`REQ-CHAIN-012`) against "All bits shall be written as
0, else the request will be rejected" (appearing twice for the two
reserved octet groups).

Left uncited, deliberately: `REQ-CHAIN-001` (`strerror()` uniqueness)
-- the same implementation-detail pattern as every prior batch.

Used the pre-flight citation-target check -- confirmed all 8 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 143. Citation backfill, batch 23: CANCEL (issue #164) (v0.143.0)

Twenty-third batch. `request_cancel.c`/`request_cancel.h`, 8/12
uncited going in. A second id-mapping error caught and fixed by the
pre-flight check before any edit ran, same class as batch 22's:
`REQ-CANCEL-007` was drafted as a target but the live scan showed it
already cited -- corrected (folded its intended citation text into
`006`, its sibling in the same source tag group) before running the
script.

Cited 7 of the 8 uncited `REQ-CANCEL-*` requirements against §11.2.3's
three cancellation mechanisms: clear-all decode validation
(`REQ-CANCEL-003`) against §11.2.3.1 Figure 13/Table 11 (request_type
0x05); clear-single decode validation (`REQ-CANCEL-006`) against
§11.2.3.3 Figure 15/Table 13 (request_type 0x07,
clear_transaction_num); the cancellable-window predicate
(`REQ-CANCEL-008`, `010`) against "A request can be cancelled while it
is pending and after it has been started until it is under execution.
Requests under execution will not be aborted."; the NOT_FOUND/CANCELED
outcome logic (`REQ-CANCEL-009`, `011`) against the same window text
plus §11.2.3.3's "error response with the error code = REQUEST_NOT_FOUND,
when the clear_transaction_num was not found" rule; and the
chain-cascade predicate (`REQ-CANCEL-012`) against §11.2.3's "If a
request is cancelled to which a request is chained, then the chained
successors shall be cancelled by the RC Server as well."

Left uncited, deliberately: `REQ-CANCEL-001` (`strerror()`
uniqueness) -- the same implementation-detail pattern as every prior
batch.

Used the pre-flight citation-target check -- confirmed all 7 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 144. Citation backfill, batch 24: SEQ (issue #164) (v0.144.0)

Twenty-fourth batch. `request_sequencer.c`/`request_sequencer.h`,
10/14 uncited going in -- the shared sequencer-state-table primitive
CMP and SRV both consume (`rcp_sequencer_get_state()`/`_set_state()`/
`_index_valid()`), previously cited only indirectly through those
callers' own citations. TC18 dedicates two sections to sequencers in
their own right, not just as compound-request sub-fields: §12.10
(Sequencers, the concept itself) and §12.7.10 (Sequencer state
registers, Table 25's SEQUENCER_config register map).

Cited all 10 of the 10 uncited `REQ-SEQ-*` requirements -- SEQ is now
100% cited: table allocation and power-on-state initialization
(`REQ-SEQ-002`-`003`, `005`) against "After power-on/reset all
sequencers are in state 1" (§12.10) and "Upon power-on reset all
sequencer state values are set to '1'" (§12.7.10); the
implementation-may-support-fewer-sequencers rule
(`REQ-SEQ-004`) against §12.10's own optionality text; table teardown
and the state-register read/write pair (`REQ-SEQ-006`, `008`, `010`)
against §12.10's "Sequencers are simply a state register" framing and
Table 25's manual-access register description, `010` also citing
Table 25's "if manually set to 0 then disabled" rule; and the
256-sequencer/state bound (`REQ-SEQ-007`, `009`, `011`) against §12.10's
own "limited to 256 by this definition" line.

Used the pre-flight citation-target check -- confirmed all 10 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 145. Citation backfill, batch 25: TIMED (issue #164) (v0.145.0)

Twenty-fifth batch, closing out the conditional-request family opened
by CMP (batch 16): CMP, SCHED, TRIG, CHAIN, CANCEL, SEQ, and now TIMED
are all cited. `request_timed.c`/`request_timed.h`, 7/13 uncited going
in. §11.2.2.5 ("Timed requests -- presentation-time based execution of
requests") is a clean, self-contained TC18 section: Figure 12/Table
10's wire format (request_type=0x0A, presentation_time[47:0]) plus two
explicit rejection rules -- PRESENTATION_TIME_TOO_FAR (implementation-
dependent horizon) and GPTP_FAIL (time sync not established).

Cited 6 of the 7 uncited `REQ-TIMED-*` requirements: feature gating
(`REQ-TIMED-002`) against the two presentation-time delivery options
(TSCF avtp_timestamp vs. ACF_GBB message_timestamp) plus the
conformance bullets naming gPTP + timed-request support as linked
capabilities; decode validation (`REQ-TIMED-004`-`005`) against the
shared §11.2.2 mtv=0-repurposing intro and Table 5's `0x0A` opcode;
the too-far-in-the-future rejection (`REQ-TIMED-006`) against the
PRESENTATION_TIME_TOO_FAR rule; the gPTP-lock admission gate
(`REQ-TIMED-007`) against the GPTP_FAIL rule; and the due-check
(`REQ-TIMED-011`) against "the presentation_time is the earliest point
in time, when the request shall be executed."

Left uncited, deliberately: `REQ-TIMED-001` (`strerror()` uniqueness)
-- the same implementation-detail pattern as every prior batch.

Used the pre-flight citation-target check -- confirmed all 6 targets
were genuinely uncited before writing anything.

Purely additive; no code or test changed. 1028 requirements (unchanged
count), 100% traced+tested, 0 `cfusa check` errors.

### 146. Phase 5a batch 1: REQ-E2E-042 quadlet-alignment enforcement (issue #197)

First milestone of Phase 5 -- the post-Phase-2 real behavioral gap-closure
effort (issues #197-201, replacing citation backfill as the active work;
see the issue #164 closing comment and `ROADMAP.md`'s own commit history
around this milestone for the full replan). Unlike every milestone since
121, this is a genuine logic change, not an additive JSON citation --
verified accordingly (mutation-tested, not just built and tested).

`rcp_e2e_wrap()` now rejects (returns a zeroed `rcp_bytes_t`) any
`acf_frame_len` that is not a whole quadlet (a multiple of 4), instead of
silently appending its CRC32 trailer at a misaligned offset. TC18 §13.6
Figures 19/20 require the CRC to span whole quadlets of the ACF message
so the trailer itself occupies the message's final whole quadlet -- a
non-quadlet-aligned input meant the trailer straddled a quadlet boundary
and the already-adapted `acf_msg_length` no longer described the actual
message. `REQ-E2E-042`'s `.fusa-reqs.json` entry flips from `tc18-gap`
(status `partial`) to a normal cited `tc18` entry.

`tests/test_tc18_gaps_e2e.c`'s own pinned-deviation test for this
requirement is rewritten to assert the corrected behavior (misaligned
input now fails safe; a properly quadlet-aligned, already-padded frame
still wraps correctly) rather than documenting the gap. Fixing this also
surfaced three quadlet-misaligned synthetic test fixtures in the older
`tests/test_e2e.c` (14, 14, and 10-octet hand-built frames, none of them
real ACF-encoder output) that the new enforcement correctly rejected --
resized to 16/16/12 octets with a comment explaining why.

Mutation-tested: reverted the fix, confirmed
`test_crc_covers_pad_octets_and_alignment_is_enforced` fails exactly as
expected, restored. Full test suite (64/64) + ASan/UBSan build both
clean. Fresh `cfusa check` (0 errors) + `cfusa trace --req-coverage 100
--sec-tested 100` (100%/100%). 1028 requirements (unchanged count), 147
`tc18-gap` entries remaining (was 148).

### 147. Phase 5a batch 2: REQ-E2E-035 NTSCF-framed wrappers (issue #197)

TC18 §13.6 requires an NTSCF-framed message (an NTSCF header carries no
timestamp field of its own) to contribute four all-zero octets as its
`avtp_timestamp` in the CRC32. `rcp_e2e_compute_crc()`/`rcp_e2e_wrap()`/
`rcp_e2e_unwrap()` take `avtp_timestamp` as a plain `uint32_t` with no
NTSCF/TSCF discriminator, so nothing prevented a caller from feeding an
NTSCF message a nonzero timestamp and producing a message a conforming
peer would reject as a CRC mismatch, with no diagnostic pointing at the
real cause.

Rather than change the raw primitives' signatures (they stay
general-purpose, serving both TSCF- and NTSCF-framed callers, and every
existing caller/test already depends on their current shape), added a
new framing-aware pair: `rcp_e2e_wrap_framed()`/`rcp_e2e_unwrap_framed()`
take an explicit `is_ntscf_framed` bool and force the zero contribution
regardless of what `avtp_timestamp` they were given, delegating to the
existing `rcp_e2e_wrap()`/`_unwrap()` for everything else. This is the
conformant entry point for a caller that already knows a message's
framing (e.g. against `avtp.h`'s own `RCP_AVTP_SUBTYPE_NTSCF`/`_TSCF`
discriminator) -- the raw primitives remain available, and still trust
the caller, for a lower-level caller that manages framing itself.

`test_tc18_gaps_e2e.c`'s `REQ-E2E-035` test is rewritten to exercise both
the raw primitives (unchanged contract, still documented) and the new
wrapper (proving it produces byte-identical output to a correctly-zeroed
raw call despite being told a nonzero timestamp, and that
`unwrap_framed()` verifies against it symmetrically).

Mutation-tested: reverting the fix doesn't just fail a test -- the test
file fails to *build* at all (`call to undeclared function
'rcp_e2e_wrap_framed'`), an even stronger signal than a runtime
assertion failure. Full test suite (64/64) + ASan/UBSan build both
clean. Fresh `cfusa check` (0 errors) + `cfusa trace --req-coverage 100
--sec-tested 100` (100%/100%). 1028 requirements (unchanged count), 146
`tc18-gap` entries remaining (was 147).

### 148. Phase 5a batch 3: REQ-E2E-030 request-storage-overflow error code (issue #197)

TC18 §12.7.7 Table 22 relative address 0x000D bit 5
(`rx_ovrflw_safestate_enable`) requires that an overflow of any one
endpoint's request storage bring every endpoint bound to that request
stream into its configured safe state. `rcp_server_endpoint_admit()`
already detected the condition (`claim_slot()` returning NULL) but
reported it as a bare `RCP_SERVER_ADMIT_REJECTED` with `*out_error`
left at `RCP_ERROR_NONE` -- the request was dropped silently, with
nothing a caller could turn into a real Table 27 error response.

This is a genuine architecture boundary, not an oversight: this
library's `rcp_server_endpoint_t` type is scoped to one endpoint, and
TC18's rule requires action across every *other* endpoint sharing the
request stream -- a concept this codebase's data model has no type for
(the same boundary `e2e.h`'s file header already documents for
`rcp_e2e_watchdog_purge_should_keep()`/`_classify()`). Rather than
overclaim full conformance or leave the gap untouched, this fix does
the honestly-achievable per-request half and documents the rest:

  - `rcp_server_endpoint_admit()`'s `claim_slot()`-exhausted path now
    sets `*out_error = RCP_ERROR_REQUEST_STORAGE_OVERFLOW` (already
    defined in `errors.h`, previously unused anywhere) before
    returning `RCP_SERVER_ADMIT_REJECTED`. `mock.c`'s
    `finish_admission()` already builds a full conformant error
    response from any non-`RCP_ERROR_NONE` `*out_error` via
    `rcp_acf_unpack_header()` + `rcp_acf_build_error_response()`, so no
    `mock.c` change was needed for this to be fully functional and
    testable end to end.
  - `rcp_e2e_overflow_should_enter_safe_state(bool
    rx_ovrflw_safestate_enable)` is a new pure primitive in `e2e.c`/
    `e2e.h`, the same "own small pure helper" shape as
    `rcp_e2e_wd_evaluate()`: trivially `rx_ovrflw_safestate_enable`,
    gated on nothing else since overflow has already happened by
    construction when it's called. It is the caller-facing decision a
    future cross-endpoint, stream-wide orchestrator would consult to
    perform the escalation this single-endpoint call cannot.

`.fusa-reqs.json`'s `REQ-E2E-030` moves from `status: "not-implemented"`
to `status: "partial"` (stays `scope: "tc18-gap"` -- the stream-wide
escalation is still genuinely missing), text rewritten to describe
exactly what's now implemented and what remains architecturally out of
this library's current scope.

`tests/test_tc18_gaps_ep.c`'s `REQ-E2E-030` deviation-pin test is
rewritten to assert the new `*out_error` value on overflow (previously
untestable, since nothing set it), plus a new direct test of
`rcp_e2e_overflow_should_enter_safe_state()`.

Mutation-tested: reverting the fix (`git stash` on `src/server.c`,
`src/e2e.c`, `include/rcp/e2e.h`) fails the test file's *build*, not
just a runtime assertion (`call to undeclared function
'rcp_e2e_overflow_should_enter_safe_state'`) -- restoring the fix
rebuilds and passes clean again. Full test suite (64/64) + ASan/UBSan
build both clean. Fresh `cfusa check` (0 errors) + `cfusa trace
--req-coverage 100 --sec-tested 100` (100%/100%). 1028 requirements
(unchanged count), 146 `tc18-gap` entries remaining (unchanged count --
this one stays a gap entry, now `partial` instead of
`not-implemented`).

### 149. Phase 5a batch 4: REQ-E2E-028/029 sequence-number enforcement primitive (issue #197)

TC18 §12.7.7 Table 22 relative address 0x000D bits 1/2
(`rx_enforce_seq`/`rx_seq_safestate_enable`) define two independent
reactions to a request stream's AVTPDU `sequence_num`: reject a request
whose sequence did not increase relative to the last accepted one
(`rx_enforce_seq`), and separately escalate every endpoint on the
stream to its configured safe state on any discontinuity -- a sequence
that advanced by more than one increment -- regardless of whether that
gap was itself accepted (`rx_seq_safestate_enable`). Neither bit nor
the per-stream sequence-counter state they gate existed anywhere in
this codebase.

This batch adds:

  - `rx_enforce_seq`/`rx_seq_safestate_enable` bool fields on
    `rcp_regmap_request_stream_cfg_t` (`regmap.h`), completing Table
    22's 0x000D byte alongside the fields Phase 18 already wired.
  - `rcp_e2e_seq_tracker_t` (`e2e.h`/`e2e.c`): a caller-owned per-stream
    tracker holding the last *accepted* `sequence_num`, the same
    "small stateful helper alongside this module's pure functions"
    shape as `rcp_e2e_stream_fault_t`.
  - `rcp_e2e_seq_evaluate()`: the pure decision function, returning
    `accept` (per `rx_enforce_seq`), `discontinuity` (a gap of more
    than one increment, independent of `accept`), and
    `enter_safe_state` (`discontinuity && rx_seq_safestate_enable`).
    Uses RFC 1982 serial-number comparison (forward distance in
    `[1, 127]` of the 256-value space) so `sequence_num`'s 8-bit
    wraparound (0xFF -> 0x00) reads as a clean single increment rather
    than a spurious rejection -- TC18's own prose does not specify
    modular comparison, but a literal always-greater-than reading would
    reject every request once the counter first wraps on any
    long-lived stream, which cannot be the intended behavior of a
    mechanism meant to run indefinitely.
  - The tracker's `prev_seq` advances **only on accept** -- a design
    decision, not an oversight: advancing on a rejected/replayed seq
    would drag the reference point backward and let a replay weaken
    detection of the genuine next request. Verified directly: a
    rejected replay leaves `prev_seq` unmoved, and the real next
    sequence number is still correctly accepted afterward.

What remains not implemented, and is documented as such rather than
overclaimed: `rcp_server_endpoint_admit()` has no `sequence_num` input
at all. The AVTPDU header (including `sequence_num`) is decoded and
discarded by whatever caller demultiplexes a frame before handing
`admit()` just the ACF payload -- e.g. `mock.c`'s
`rcp_mock_server_dispatch_frame()`. Wiring `rcp_e2e_seq_evaluate()`
into a real admission path needs `sequence_num` threaded through that
caller, a larger, separate integration change not undertaken here.
`rx_seq_safestate_enable`'s escalation itself also shares
`REQ-E2E-030`'s already-documented cross-endpoint architecture
boundary. `.fusa-reqs.json`'s `REQ-E2E-028`/`REQ-E2E-029` both move
`not-implemented` -> `partial`.

`tests/test_tc18_gaps_ep.c` gains 6 new direct tests of
`rcp_e2e_seq_evaluate()` (first call, single increment, gap-with/without
escalation, replay-rejected-tracker-unmoved, `rx_enforce_seq` off,
wraparound) plus updated doc comments on the two existing
admission-level deviation pins clarifying they're still accurate (the
primitive exists; nothing calls it yet).

Mutation-tested: reverting the fix (`git stash` on `src/e2e.c`,
`include/rcp/e2e.h`, `include/rcp/regmap.h`) fails the test file's
*build* (20 errors: undeclared `rcp_e2e_seq_tracker_init`/
`rcp_e2e_seq_evaluate`, unknown type `rcp_e2e_seq_tracker_t`) --
restoring the fix rebuilds and passes clean again. Full test suite
(64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) +
`cfusa trace --req-coverage 100 --sec-tested 100` (100%/100%). 1028
requirements (unchanged count), 146 `tc18-gap` entries remaining
(unchanged count -- both stay gap entries, now `partial`).

### 150. Phase 5a batch 5: REQ-E2E-031/033/041 CRC verification wired into mock.c's dispatch path (issue #197)

Batches 3-4 both landed real, correct, pure primitives
(`rcp_e2e_overflow_should_enter_safe_state()`,
`rcp_e2e_seq_evaluate()`) that turned out to be unreachable from any
real call path: `mock.c`'s dispatch pipeline
(`rcp_mock_server_dispatch()`/`_dispatch_frame()`) never threads
AVTP-level context (`stream_id`, `avtp_timestamp`, `sequence_num`) down
to where `rcp_server_endpoint_admit()` or any E2E primitive could
consume it -- the AVTPDU header is decoded and discarded by whatever
caller demultiplexes a frame, one layer above `dispatch_frame()`. This
batch is the deferred "biggest architecture decision in the phase":
wiring CRC verification into a real dispatch path for the first time.

TC18 §13.6's CRC-mismatch handling was already correct at the
primitive level (`rcp_e2e_unwrap()` reports `RCP_E2E_ERR_CRC_MISMATCH`,
`rcp_e2e_wire_error()` maps it to `RCP_ERROR_POCI_FAILURE`) -- what was
missing was any caller and any error-response emitter
(`REQ-E2E-041`), any code path consulting `ep_req_crc_enable`
(`REQ-E2E-031`), and any per-member verification across a multi-ACF
frame (`REQ-E2E-033`).

Following the established API-safety pattern (never break
`rcp_mock_server_dispatch()`/`_dispatch_frame()`'s existing signatures
-- 4 real call sites across the test suite depend on them unchanged),
this batch adds:

  - `rcp_mock_server_set_endpoint_req_crc_enable()`: a new setter
    storing this test double's own in-process stand-in for TC18
    §12.7.1's `ep_req_crc_enable` on a new field of the internal
    (non-public) `rcp_mock_endpoint_slot_t`. Necessary because
    `ep_req_crc_enable` lives in `rcp_regmap_ep_functional_cfg_t`,
    which every concrete endpoint type's own cfg struct composes as a
    different concrete type -- `mock.c`'s type-erased dispatch layer
    has no generic way to read it, the same architecture reason every
    other config field `mock.c` models (`ep_enable`, `ep_delay_time`,
    ...) is already its own direct C-API setter rather than a
    register-map read.
  - `RCP_MOCK_DISPATCH_CRC_ERROR`: a new, additive
    `rcp_mock_dispatch_result_t` enum value (appended, not
    renumbering).
  - `rcp_mock_server_dispatch_e2e()`: `rcp_mock_server_dispatch()`'s
    E2E-aware counterpart. For an endpoint with `req_crc_enable` set,
    `request` is first run through `rcp_e2e_unwrap_framed()`
    (`is_ntscf_framed` derived from `avtp_subtype ==
    RCP_AVTP_SUBTYPE_NTSCF`, not a separate parameter, so a caller
    cannot pass an inconsistent combination of the two). On
    `RCP_E2E_ERR_CRC_MISMATCH`: `rcp_server_endpoint_admit()` is never
    called at all (not admitted, not merely not-executed) and
    `rcp_acf_build_error_response()` builds a genuine Table 27
    `POCI_FAILURE` response, byte_bus_id/transaction_num read back out
    of the request's own (CRC-failure-unaffected) header via
    `rcp_acf_unpack_header()` -- the exact pattern `finish_admission()`
    already established for other determined error codes. On
    `RCP_E2E_ERR_SHORT_FRAME`: same rejection, but `*out_response` is
    left zeroed (too short to read a `transaction_num` from, the same
    "`RCP_ERROR_NONE` means no determined code" convention this
    codebase already follows elsewhere). On `RCP_E2E_OK`: dispatch
    proceeds against the unwrapped header-and-payload region via the
    existing `rcp_mock_server_dispatch()`, unchanged otherwise. An
    endpoint without `req_crc_enable` set (including an unknown
    `byte_bus_id`) delegates to `rcp_mock_server_dispatch()` outright --
    "plain command mode" is untouched.
  - `rcp_mock_server_dispatch_frame_e2e()`: `rcp_mock_server_dispatch_frame()`'s
    E2E-aware counterpart -- identical member-splitting/chaining logic,
    but each member routed through `rcp_mock_server_dispatch_e2e()`
    instead, so each E2E-protected member of a multi-ACF frame is
    verified against its own CRC32 individually (`REQ-E2E-033`): a
    corrupted second member does not block, and is not masked by, a
    valid first member.

`.fusa-reqs.json`'s `REQ-E2E-031`/`REQ-E2E-033`/`REQ-E2E-041` all move
`scope: "tc18-gap"`/`status: "partial"` -> `scope: "tc18"` (no
`status` field, matching this file's dominant convention for a fully
implemented entry -- see this milestone's own verification note below
for why the earlier `REQ-E2E-035`/`REQ-E2E-042` fixes' `status`-field
removal, not `"implemented"` retained, is the correct precedent to
follow: 849 of 882 `scope: "tc18"` entries in this file have no
`status` field at all).

7 new tests in `tests/test_tc18_gaps_e2e.c`: plain-mode-executes,
safe-mode-executes-a-valid-request, safe-mode-rejects-an-unprotected-
request, dispatch_frame_e2e-verifies-each-member-independently, and
dispatch_e2e-crc-mismatch-yields-a-real-error-response -- replacing the
three now-obsolete "DEVIATION PIN" tests (which asserted the exact
absence this batch fixes) while keeping the underlying raw-primitive
assertions those tests also made (still accurate, unchanged
contracts).

Mutation-tested: reverting the fix (`git stash` on `src/mock.c`,
`include/rcp/mock.h`) fails the test file's *build* (12 errors:
undeclared `rcp_mock_server_dispatch_e2e`/`_dispatch_frame_e2e`/
`_set_endpoint_req_crc_enable`, undeclared `RCP_MOCK_DISPATCH_CRC_ERROR`)
-- restoring the fix rebuilds and passes clean again. Full test suite
(64/64) + ASan/UBSan build both clean (this batch is the first to add
new heap-allocating/freeing code paths this session, making the ASan
pass especially load-bearing here -- clean). Fresh `cfusa check` (0
errors) + `cfusa trace --req-coverage 100 --sec-tested 100`
(100%/100%). 1028 requirements (unchanged count), 143 `tc18-gap`
entries remaining (was 146 -- three genuinely closed this batch, the
first net decrease since Phase 5a began).

### 151. Phase 5a batch 6: REQ-E2E-037 AVTPDU data-length adjustment helper (issue #197)

TC18 §13.6 requires an AVTPDU's `ntscf_data_length`/`stream_data_length`
field to grow by 4 octets for every E2E-protected ACF message its
payload carries. Unlike every other item closed in Phase 5a so far,
this one turned out not to be a behavioral defect at all:
`rcp_avtp_encode_ntscf()`/`_encode_tscf()` already recompute the length
field from the actual payload buffer they're given, never from a
caller-supplied value (proven by the existing test, which deliberately
passes a bogus `hdr.ntscf_data_length` and confirms the encoder ignores
it) -- so the +4-per-protected-member accounting was already correct
by construction whenever a caller concatenated `rcp_e2e_wrap()`'s own
output per protected member first. What was actually missing, per the
requirement's own text, was purely that "no function expresses the
adjustment" -- the rule had no named, callable, testable form of its
own.

Adds `rcp_e2e_data_length_for_protected_members(size_t
protected_member_count)` to `e2e.h`/`e2e.c`, right alongside
`rcp_e2e_length_with_crc()` (the existing precedent for "a pure
arithmetic expression of a length-accounting rule, for a caller that
wants to reason about or pre-validate it independently of actually
building the payload"): `protected_member_count * RCP_E2E_CRC_LEN`,
saturating at `SIZE_MAX` on overflow rather than wrapping, same
discipline as its neighbor.

`tests/test_tc18_gaps_e2e.c`'s `REQ-E2E-037` test is updated to assert
against the new named function instead of a bare `2u *
RCP_E2E_CRC_LEN` literal, plus a new direct test of the function itself
(zero members, one member, five members, and the overflow-saturation
case).

`.fusa-reqs.json`'s `REQ-E2E-037` moves `scope: "tc18-gap"`/`status:
"partial"` -> `scope: "tc18"` (fully implemented, no `status` field).

Mutation-tested: reverting the fix (`git stash` on `src/e2e.c`,
`include/rcp/e2e.h`) fails the test file's *build* (`call to
undeclared function 'rcp_e2e_data_length_for_protected_members'`) --
restoring the fix rebuilds and passes clean again. Full test suite
(64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) +
`cfusa trace --req-coverage 100 --sec-tested 100` (100%/100%). 1028
requirements (unchanged count), 142 `tc18-gap` entries remaining (was
143).

### 152. Phase 5a batch 7 (final): REQ-E2E-038 fragmented-message CRC coverage primitive (issue #197)

TC18 §13.6 requires a fragmented message's CRC32 to span the stream_id
and avtp_timestamp of the FIRST AVTPDU, the FIRST fragment's ACF
header, and the concatenated `byte_msg_payload` of EVERY segment in
order -- appended only to the last segment's message, with only that
last message's `acf_msg_length` and only the last AVTPDU's data-length
field adapted. c-RCP's documented integration (`fragment.h`'s own file
header) wraps only the final fragment's own encoded bytes via
`rcp_e2e_wrap()`, so its CRC covers the final fragment's header and
payload alone -- every earlier segment is entirely unprotected.

This is the largest, most architecturally involved item in Phase 5a,
flagged from the very first batch as deferred pending clearer picture
of the full integration surface. Now that batches 1-6 have mapped that
surface thoroughly (culminating in batch 5's discovery that `mock.c`
has no AVTP-context-aware dispatch machinery at all), the honest scope
for this batch is clear: add the missing CRC-arithmetic primitive
correctly and completely; do not attempt to also build a
fragmented-message dispatch path in `mock.c` from scratch (there isn't
even an *unprotected* one to extend -- `dispatch_frame()`/
`_dispatch_frame_e2e()` both split multiple ACF messages *within one
AVTPDU*, not one message split *across* multiple AVTPDUs, which is
what fragment.h's own mechanism produces). That's a materially larger,
separate architecture item, not a natural extension of this batch.

Adds `rcp_e2e_compute_fragmented_crc(stream_id, avtp_timestamp,
first_fragment_header, first_fragment_header_len,
reassembled_payload, reassembled_payload_len)` to `e2e.h`/`e2e.c`,
right alongside `rcp_e2e_fragment_carries_crc()` in the "Fragmentation/
CRC interaction" section: the same running-CRC-over-several-regions
technique `rcp_e2e_compute_crc()` itself already uses for its own three
regions (stream_id/avtp_timestamp/acf_frame), extended to four
(stream_id/avtp_timestamp/first-fragment-header/reassembled-payload).
`fragment.h`'s own `rcp_fragment_reassembler_get()` already produces
exactly the "concatenated payload of every segment in order" this
function's `reassembled_payload` parameter needs on the decode side;
an encode-side caller assembles the same by concatenating each
`rcp_fragment_plan()` segment's own payload slice in order.

`tests/test_tc18_gaps_e2e.c`'s `REQ-E2E-038` test is updated to build
its "what a conforming implementation would compute" reference value
via the new named function instead of a hand-rolled byte-concatenation
buffer, keeping the same two behavioral assertions (segment-0
corruption moves the conforming CRC; `rcp_e2e_wrap()`-based
verification still doesn't notice). A new direct test confirms the
function is equivalent to concatenating header++payload and calling
`rcp_e2e_compute_crc()` once, and is sensitive to a change anywhere in
either region (not just the payload's tail, where the old,
pre-REQ-E2E-042 `rcp_e2e_wrap()`-only approach would have been blind).

`.fusa-reqs.json`'s `REQ-E2E-038` moves `status: "not-implemented"` ->
`"partial"` (stays `scope: "tc18-gap"` -- the primitive is correct, but
still genuinely uncalled from anywhere in this codebase; not
overclaimed as `implemented`).

Mutation-tested: reverting the fix (`git stash` on `src/e2e.c`,
`include/rcp/e2e.h`) fails the test file's *build* (`call to
undeclared function 'rcp_e2e_compute_fragmented_crc'`) -- restoring the
fix rebuilds and passes clean again. Full test suite (64/64) +
ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) + `cfusa
trace --req-coverage 100 --sec-tested 100` (100%/100%). 1028
requirements (unchanged count), 142 `tc18-gap` entries remaining
(unchanged count -- stays a gap entry, now `partial` instead of
`not-implemented`).

**Phase 5a (issue #197) is functionally complete as of this milestone**:
all 12 originally catalogued items (`REQ-E2E-028` through `-042`, minus
the `-032`/`-034`/`-036`/`-040`/`-042`-numbering already-implemented
entries not needing a fix) have real, tested, mutation-verified fixes
landed across 7 PRs (#202-#207 plus this one). Every fix that could be
honestly claimed `implemented` was; three (`REQ-E2E-029`/`-030`/`-038`)
remain deliberately `partial`, each with a clearly documented
architecture boundary (cross-endpoint escalation, or a dispatch
integration surface that does not exist yet) rather than an
overclaimed status.

### 153. Phase 5b batch 1: REQ-LIFECYCLE-028 HW_CONFIGURED drops TSCF unconditionally (issue #198)

First batch of Phase 5b (LIFECYCLE access-control gaps, issue #198, 16
requirements). `rcp_lifecycle_should_accept()` accepted TSCF-headed
AVTPDUs in `HW_CONFIGURED` whenever the node advertised time-sync
support (only the general `rcp_avtp_should_drop_tscf()` rule applied),
whereas TC18 §12.3.1.2 requires them dropped unconditionally in that
state too, the same rule already correctly applied to
`HW_UNCONFIGURED`.

**Primary-source verification finding, worth recording**: §12.3.1.2's
own printed heading in the real TC18 PDF confusingly repeats
"Lifecycle state HW_UNCONFIGURED" (the same heading as the correctly-
labeled §12.3.1.1 immediately above it) -- a genuine typo in the
source document, not a `pdftotext`/`TC18.txt`-extraction artifact
(checked by re-extracting the raw PDF pages directly, not just the
pre-existing line-numbered text dump, before trusting it). The
section's actual *content* is unambiguous regardless of the wrong
heading: "access to the HW_config...shall have been concluded and
locked", advancing `svr_lifecycle_state` to `RCP_CONFIGURED`, root-
client write access -- all `HW_CONFIGURED` concepts, none of them
`HW_UNCONFIGURED`'s. `.fusa-reqs.json`'s own pre-existing citation
already correctly attributed this text to `HW_CONFIGURED` despite the
heading; this milestone's fix confirms that attribution against the
primary source before implementing it, per this project's standing
"verify tools/citations against primary sources" discipline.

**Scope split from the sibling REQ-LIFECYCLE-029 finding, deliberately
not implemented together**: §12.3.1.2 also requires dropping ACF_GBB-
format requests in `HW_CONFIGURED`. Implementing that alongside this
TSCF fix was attempted first and immediately reverted: every
conditional request kind (compound/compound-wait/triggered/chained/
timed/cancel) is wire-encoded as ACF_GBB unconditionally (the mtv-
repurposing scheme `request_compound.h` and siblings document), so an
unconditional ACF_GBB drop in `HW_CONFIGURED` would make it impossible
to ever admit a conditional request there at all -- breaking 25 of 27
tests in `test_conditional_dispatch.c`, whose entire fixture submits
conditional requests against a `HW_CONFIGURED` server. This is
entangled with `REQ-LIFECYCLE-032` (HW_CONFIGURED traffic should be
restricted to configuration requests only -- not yet implemented
either): closing `-032` first is expected to resolve `-029` largely for
free, the same "shared plumbing first" reasoning issue #198's own
suggested implementation order already calls for. `REQ-LIFECYCLE-029`
stays `not-implemented`, its `.fusa-reqs.json` text updated to record
this reasoning for whichever batch tackles it next.

Fixing REQ-LIFECYCLE-028 alone also surfaced and required updating:
`tests/test_lifecycle.c`'s own pre-existing (pre-gap-audit,
milestone-61-era) `test_hw_configured_accepts_tscf_when_time_sync_
supported`, which asserted the now-superseded permissive behavior as
correct -- renamed and rewritten to assert the new, spec-verified
behavior instead, the same "an older test can itself be wrong, not
just a deviation pin" pattern REQ-E2E-042 first established in Phase
5a. Also required moving two `test_tc18_gaps_e2e.c` fixtures (added in
Phase 5a batch 5, dispatching real nonzero-timestamp TSCF-framed
traffic against a `HW_CONFIGURED` server) to `RCP_CONFIGURED` instead,
via a new `to_rcp_configured()` helper -- TSCF is unrestricted again
once `RCP_CONFIGURED`, where the mapping HW_CONFIGURED's new rule
guards against not existing yet has, by then, been validated.

`.fusa-reqs.json`'s `REQ-LIFECYCLE-028` moves `scope: "tc18-gap"`/
`status: "not-implemented"` -> `scope: "tc18"` (fully implemented, no
`status` field).

Mutation-tested: reverting the fix (`git stash` on `src/lifecycle.c`,
`include/rcp/lifecycle.h`) fails `test_hw_configured_drops_tscf`'s own
new assertion (`Expected FALSE Was TRUE`) and the renamed
`test_lifecycle.c` test, a runtime assertion failure rather than a
build break (no new function/type was added, only a branch's
behavior) -- restoring the fix passes clean again. Full test suite
(64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0 errors)
+ `cfusa trace --req-coverage 100 --sec-tested 100` (100%/100%). 1028
requirements (unchanged count), 141 `tc18-gap` entries remaining (was
142).

### 154. Phase 5b batch 2: REQ-LIFECYCLE-032 HW_CONFIGURED admits only EP0 (issue #198)

TC18 §12.3.1.2 requires "requests to EPs other than EP0 that are not
config requests" to be ignored and dropped without response while
HW_CONFIGURED. `rcp_lifecycle_should_accept()` had no such restriction
at all -- every byte_bus_id was admitted (subject only to the
REQ-LIFECYCLE-028 TSCF rule), so an operational request could be
executed or queued against any endpoint before the server had even
reached RCP_CONFIGURED.

**Architecture finding this batch turns on**: c-RCP has no wire-level
encode/decode pair for a functional-configuration read/write request
at all, for any endpoint type. `regmap.h`'s own file header documents
this as deliberately deferred ("Exact on-wire encodings for the
structures below are deliberately left unimplemented... wiring them
to an actual byte_message_info read/write exchange is later phases'
job"), confirmed by grepping the entire codebase for any regmap
encode/decode pair and finding none outside `discovery.c`'s own
read-only discovery-response encoder. Every wire request this library
can currently decode for a non-EP0 endpoint is therefore, by
construction, operational -- so the honestly-achievable, currently-
correct form of TC18's rule is simply: `rcp_lifecycle_should_accept()`
now additionally requires `byte_bus_id ==
RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID` (EP0) in `HW_CONFIGURED`, the
same restriction `HW_UNCONFIGURED`'s branch already applies.

**Attempted once already, in this same batch, and reverted mid-way**:
the fix alone breaks not just narrow gap-pin tests but `test_mock.c`'s
own foundational plumbing suite (8 of its core `add_endpoint`/
`dispatch`/`drain`/`dispatch_frame` tests -- none of them gap-specific)
plus 25 of 27 tests in `test_conditional_dispatch.c` plus 1 in
`test_tc18_gaps_e2e.c` -- ~35 existing tests across 3 files, all built
on the implicit assumption that ordinary per-endpoint dispatch already
works in `HW_CONFIGURED`. Rather than push that through blind, the fix
was reverted, the finding posted to issue #198 (comment `5235892998`),
and this batch redone as the two-step plan the write-up recommended:

  1. Migrate every affected fixture from `HW_CONFIGURED` to
     `RCP_CONFIGURED` first, verified independently of the fix itself
     (a `to_rcp_configured()` helper added to `test_mock.c` and
     `test_tc18_gaps_e2e.c`, mirroring the one already added to
     `test_tc18_gaps_e2e.c` in batch 1; `test_conditional_dispatch.c`'s
     single shared `fixture()` helper gets a second
     `rcp_mock_server_transition()` call). Each of `test_mock.c`'s 13
     `to_hw_configured()` call sites was individually inspected --
     9 needed the swap (all dispatch to a non-EP0 byte_bus_id), 4
     correctly did not (2 test the transition itself and must stay
     `HW_CONFIGURED`; 2 test frame-parsing/bus-decode failures that
     never reach `rcp_lifecycle_should_accept()` at all, so lifecycle
     state is irrelevant to them) -- not a blind find/replace.
     `EMPTY_SNAP`'s zero endpoint/request-stream counts trivially
     satisfy both plausibility checks along the way, the same
     technique batch 1 already established. This step alone (fix not
     yet reapplied) was verified to leave both files' full suites
     passing, proving the migration is safe independent of the fix.
  2. Reapply the `rcp_lifecycle_should_accept()` fix on top -- now a
     clean, isolated diff.

Also required, once the fix was back: rewriting
`test_tc18_gaps_server.c`'s `test_hw_configured_admits_any_stream_and_
any_endpoint` (previously conflating three separate gaps --
REQ-LIFECYCLE-025/032/036 -- into one test) into two focused tests:
`test_hw_configured_admits_only_ep0` (the REQ-LIFECYCLE-032 fix itself)
and `test_admit_takes_no_lifecycle_state_or_stream_identity` (the
still-open REQ-LIFECYCLE-025/036 deviation pin, one layer down at
`rcp_server_endpoint_admit()`, unaffected by this fix); and retargeting
the `REQ-LIFECYCLE-029` deviation pin from byte_bus_id 7 to EP0, since
byte_bus_id 7 is now excluded by the new restriction before its
`acf_msg_type` is ever consulted, no longer exercising the ACF_GBB gap
it was meant to pin.

`.fusa-reqs.json`'s `REQ-LIFECYCLE-032` moves `scope: "tc18-gap"`/
`status: "not-implemented"` -> `scope: "tc18"` (fully implemented, no
`status` field). `REQ-LIFECYCLE-029`'s text updated to record its
narrowed remaining scope (only an EP0-addressed conditional request
now escapes it, an edge case no test in this codebase currently
exercises) -- still `not-implemented`, not overclaimed.

Mutation-tested: reverting the fix (`git stash` on `src/lifecycle.c`,
`include/rcp/lifecycle.h`) fails `test_hw_configured_admits_only_ep0`'s
own new assertion (`Expected FALSE Was TRUE`), a runtime assertion
failure rather than a build break (no new function/type was added,
only a branch's behavior) -- restoring the fix passes clean again.
Full test suite (64/64) + ASan/UBSan build both clean. Fresh `cfusa
check` (0 errors) + `cfusa trace --req-coverage 100 --sec-tested 100`
(100%/100%). 1028 requirements (unchanged count), 140 `tc18-gap`
entries remaining (was 141).

### 155. Phase 5b batch 3: REQ-LIFECYCLE-027 write requests unicast-only (issue #198)

TC18 §12.3.1.1, §12.3.1.2 and §12.3.1.3 each restate (once per
lifecycle state) that a write request is accepted only when carried in
a unicast frame -- "A single broadcast/multicast write frame can
therefore reconfigure every RC Server on the network at once" is the
issue's own framing for why this was flagged the single highest-
severity item in the whole phase. Neither `rcp_lifecycle_should_accept()`
nor `rcp_lifecycle_field_writable()` took any destination-MAC input at
all, so an identical write carried in a multicast or broadcast frame
was processed exactly like a unicast one.

**Blast-radius check done before committing to scope** (the batch-2
lesson applied directly this time): `rcp_lifecycle_field_writable()` is
called from 12 endpoint `_writable()` wrappers across every endpoint
type, but every one of them just forwards an already-constructed
`rcp_lifecycle_writer_ctx_t writer` parameter -- none of them construct
one locally. The only real (non-test) construction site in the whole
codebase is `rcp_regmap_writer_ctx()`, with 8 call sites, all in
`tests/test_regmap.c`. Adding a *field* to `rcp_lifecycle_writer_ctx_t`
rather than a new bare function parameter, and naming it so its
zero-initialized default means "unicast/compliant" rather than
"non-unicast", means every one of the ~150 existing `{...}`/`{0}`
writer_ctx literals across every `test_ep_*.c`/`test_lifecycle.c`/
`test_tc18_gaps_*.c` file needs **no changes at all** -- C's partial-
brace-initializer rule zero-initializes a struct's unlisted trailing
members, so `{true, false}` for the now-3-field struct still means
exactly what it meant before. Only `rcp_regmap_writer_ctx()`'s 8 call
sites (a new function *parameter*, not a struct literal) needed
updating, to `true` (preserving their currently-tested unicast/
compliant behavior) -- confirmed by `grep -rn` for both symbols before
writing a single line, per the standing post-batch-2 checklist.

**New primitive**: `rcp_l2_mac_is_unicast()` (`l2.h`/`l2.c`,
`REQ-L2-011`) -- the IEEE 802.3 individual/group bit (LSB of the first
octet) classifier a real integrator needs to turn a frame's destination
MAC (from `rcp_l2_frame_decode()`'s `out_dst_mac` or a transport's own
equivalent) into the new writer_ctx field. The all-ones broadcast
address is correctly classified not-unicast as a special case of
multicast under the same bit test, no separate check needed.

**The fix itself**: `rcp_lifecycle_writer_ctx_t` gains
`via_non_unicast_frame`; `rcp_lifecycle_field_writable()` computes its
existing per-kind/per-state `writable` verdict exactly as before, then
returns `writable && !writer.via_non_unicast_frame` -- one trailing AND
rather than duplicating the check across three kinds and every state,
covering `HW_GENERIC` (the write path actually exercised during
`HW_UNCONFIGURED`, per TC18 §12.3.1.1's own paragraph), `FUNCTIONAL_W`
and `FUNCTIONAL_W_STAR` uniformly. `rcp_regmap_writer_ctx()` gains a
`via_unicast` parameter and sets `ctx.via_non_unicast_frame =
!via_unicast` -- the one production derivation path this library has
today, closing the loop from a real classified MAC through to the
gate. `rcp_lifecycle_should_accept()` deliberately still takes no
destination-MAC input: TC18's unicast rule is scoped to write requests
specifically, not general frame admission, so gating only at
`field_writable()` -- where the write path already lives -- is the
architecturally correct split, not a shortcut.

New tests: `test_l2.c` pins the unicast/multicast/broadcast
classification (including the all-zero MAC, I/G bit clear, correctly
still unicast); `test_lifecycle.c` pins `field_writable()` denying an
otherwise-writable field across all three kinds purely on
`via_non_unicast_frame`, independent of authorization; `test_regmap.c`
pins `via_unicast` plumbing straight through to
`via_non_unicast_frame`, independent of root-client/owning-stream
authorization. `test_tc18_gaps_server.c`'s pre-existing
`test_hw_configured_write_access_is_unrestricted()` comment, which had
described this exact gap, updated to record its closure and point at
the new dedicated coverage rather than left stale.

Mutation-tested three ways: (1) `git stash` on `src/l2.c` alone ->
build breaks (`Undefined symbols ... _rcp_l2_mac_is_unicast`), the
new-API build-break signature; (2) `git stash` on `src/lifecycle.c`
alone -> exactly one runtime failure, the new pinned
`test_field_writable_denies_non_unicast_frame_regardless_of_kind_or_
authorization` (`Expected FALSE Was TRUE`), all 31 other
`test_lifecycle.c` tests still pass; (3) `git stash` on `src/regmap.c`
alone -> build breaks (`conflicting types for 'rcp_regmap_writer_ctx'`,
header/impl signature mismatch), plus a further precise single-line
mutation (inverting `!via_unicast` to `via_unicast` without touching
the signature) isolates the assignment itself -> exactly the new
`test_writer_ctx_plumbs_via_unicast_to_non_unicast_frame_flag` fails
(`Expected FALSE Was TRUE`). All three restored clean. Full test suite
(64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) +
`cfusa trace --req-coverage 100 --sec-tested 100` (100%/100%). 1029
requirements (was 1028, `REQ-L2-011` added), 139 `tc18-gap` entries
remaining (was 140).

### 156. Phase 5b batch 4: REQ-LIFECYCLE-030 + REQ-LIFECYCLE-036 HW_CONFIGURED write authorization (issue #198)

Both requirements reduce to the identical condition once examined
closely: TC18 §12.3.1.2 (REQ-LIFECYCLE-030, EP0 cross-endpoint access)
and §12.7.3 (REQ-LIFECYCLE-036, direct-to-endpoint access) each require
HW_CONFIGURED functional-config write access be gated on the root
client via EP0, the endpoint's own owning stream, or the discovery
stream -- `rcp_lifecycle_field_writable()`'s `HW_CONFIGURED` branches
for `FUNCTIONAL_W`/`FUNCTIONAL_W_STAR` previously `return true`
unconditionally. Closed both in one change: a new
`via_discovery_stream` member on `rcp_lifecycle_writer_ctx_t` (the
`§12.7.3(a)` case; `via_owning_stream`, already modeled, is `(b)`'s
"known stream_id/byte_bus_id pair"), and `HW_CONFIGURED`'s two branches
now gate on `authorized || writer.via_discovery_stream` (`authorized`
being the same `via_root_client_ep0 || via_owning_stream` formula
`RCP_CONFIGURED` already used) rather than unconditional `true`.
`RCP_CONFIGURED`'s own rule is deliberately left untouched -- it does
not (yet) admit the discovery stream on its own, a distinct, still-open
§12.7.4 concern this batch does not attempt.

**Blast-radius check applied proactively, following batch 3's own
discipline, and it paid off**: grepped every `HW_CONFIGURED` +
`FUNCTIONAL_W`/writability call site before writing any code. Found a
real, larger-than-`-027` blast radius up front -- 11 `test_ep_*.c`
files (23 raw `writer = {0}` declarations, 14 bare/unauthorized-as-
declared), `test_lifecycle.c`'s own bug-documenting test, a DEVIATION
PIN in `test_tc18_gaps_server.c`, a shared `any_writer()` helper in
`test_tc18_gaps_ep2.c` used by ~20 unrelated tests, and 3 sites in
`test_tc18_gaps_regmap.c` -- and posted the finding to issue #198
*before* touching any implementation code, per the standing lesson from
`REQ-LIFECYCLE-032`'s revert-and-redo. Applying the fix and using the
resulting failure list (rather than hand-enumeration) to drive the
migration kept every fix targeted and verified:

  - `test_tc18_gaps_ep2.c`'s `any_writer()` -- a single shared helper
    used by ~20 tests whose real purpose is testing block layout/
    register width, not lifecycle policy -- fixed once
    (`w.via_owning_stream = true`), clearing every one of its call
    sites in one edit.
  - 20 `test_ep_*.c` "applies_when_authorized" tests across 9 files, all
    following an identical shape (a bare `writer = {0}` convenience
    default for testing the endpoint's own setter validation, not
    authorization policy -- the real authorization test lives in each
    file's separate `..._rejects_unauthorized`/`..._requires_
    authorization` sibling) -- migrated mechanically via a script that
    locates each named function and inserts `writer.via_owning_stream =
    true;` after its declaration.
  - 11 `test_ep_*.c` "any_writer" DEVIATION-PIN-style tests, one per
    endpoint type, all sharing the identical
    `test_functional_cfg_writable_true_hw_configured_any_writer` naming
    convention -- rewritten (scripted, per-file, extracting each file's
    own wrapper function name) into
    `..._hw_configured_requires_authorization_or_discovery_stream`,
    asserting `FALSE` for `none`, `TRUE` for `via_ep0`/`via_stream`/
    `via_discovery`, mirroring each file's own pre-existing
    `RCP_CONFIGURED`-requires-authorization sibling's shape.
  - `test_lifecycle.c`'s own `test_functional_w_writable_by_anyone_in_
    hw_configured` (name documented the bug directly) split into
    `test_functional_w_not_writable_in_hw_unconfigured` (unaffected,
    kept as-is) and a new
    `test_functional_w_hw_configured_requires_authorization_or_
    discovery_stream`.
  - `test_tc18_gaps_server.c`'s `test_hw_configured_write_access_is_
    unrestricted` -- a DEVIATION PIN documenting exactly this gap
    (plus the unicast half batch 3 already closed) -- renamed to
    `test_hw_configured_write_access_now_requires_unicast_and_
    authorization` and rewritten to assert the corrected, composed
    behavior: authorization alone, unicast alone, and both together
    (an authorized-but-non-unicast writer still refused).
  - `test_tc18_gaps_regmap.c`'s 3 sites -- 2 swapped `PLAIN_WRITER` for
    `ROOT_WRITER` where the test's real point was a state-only property
    (no read-only class exists; Table 22's W*-vs-read-only-by-state
    shape) unrelated to writer authorization; 1
    (`test_configuration_lock_register_is_absent`) rewritten to compare
    two *differently-authorized* writer contexts instead of an
    authorized-vs-unauthorized pair, preserving its real point (no
    separate lock byte exists -- the answer depends only on state and
    authorization, not on which authorized path was used) while also
    keeping (not dropping) a `PLAIN_WRITER`-refused assertion to
    document the now-real authorization behavior explicitly.

Mutation-tested: `git stash` on `src/lifecycle.c` alone reproduces
exactly the same 13-binary failure set the pre-fix build showed before
any test migration -- confirms every rewritten/migrated test correctly
pins the new behavior rather than accidentally becoming a tautology.
Restored clean. Full test suite (64/64) + ASan/UBSan build both clean.
Fresh `cfusa check` (0 errors) + `cfusa trace --req-coverage 100
--sec-tested 100` (100%/100%). 1029 requirements (unchanged count --
no new REQ ids this batch), 137 `tc18-gap` entries remaining (was 139).

### 157. Phase 5b batch 5: REQ-LIFECYCLE-031 svr_lifecycle_state write authorization (issue #198)

A different call path than batches 3/4: TC18 §12.3.1.2 requires a
`svr_lifecycle_state` write (the request that actually drives
`rcp_lifecycle_transition()`) be accepted only via the discovery
stream, from the configured root client, or -- with no root client
configured -- from any valid stream_id/byte_bus_id combination.
`rcp_lifecycle_transition()` previously accepted no writer input at
all: any caller could promote or demote the server's lifecycle state
unconditionally.

**Fix**: `rcp_lifecycle_transition()` gains a `rcp_lifecycle_writer_ctx_t
writer` parameter (moved the struct's own typedef earlier in the header,
ahead of both its consumers now). A new `RCP_LIFECYCLE_ERR_UNAUTHORIZED`
error code (value 4, additive) is returned, `*state` left unchanged,
when `!(writer.via_discovery_stream || writer.via_root_client_ep0)` for
the `HW_CONFIGURED -> RCP_CONFIGURED` advance or either reset-to-
`HW_UNCONFIGURED` transition. The bootstrap `HW_UNCONFIGURED ->
HW_CONFIGURED` advance deliberately does not consult `writer` at all --
TC18 §12.3.1.1 requires only that the request travel via the discovery
stream, already enforced one layer up by `rcp_lifecycle_should_accept()`'s
own `HW_UNCONFIGURED` branch (no root client can even exist yet at this
point in bring-up).

**Honestly scoped as `partial`, not `implemented`**: TC18's exact text
further narrows the non-discovery-stream case -- "If a root client is
configured only the root client's stream_id/byte_bus_id is accepted" --
implying any valid stream_id/byte_bus_id combination should suffice
when no root client is configured at all. This library has no
wire-level concept of "a valid stream_id/byte_bus_id combination"
independent of a specific endpoint's own owning stream for a
server-level field like `svr_lifecycle_state` (the same underlying
architecture gap `REQ-LIFECYCLE-025`/`034` already identified and left
open). `rcp_lifecycle_transition()` conservatively requires
`via_root_client_ep0` in both the root-client-configured and
no-root-client cases until that classification exists, rather than
accepting an unqualified stream it cannot actually validate.

**Blast radius small and fully enumerable this time, unlike batches
3/4**: adding a *required* parameter (no default to silently preserve)
to a function with only ~20 total call sites across production code
(`src/mock.c`'s one passthrough wrapper) and 5 test files meant every
affected site had to be visited explicitly by the compiler's own "too
few arguments" errors -- no shared-default surprise was possible by
construction, the opposite lesson from batch 4's `any_writer()`
discovery: sometimes a required parameter is the right shape precisely
*because* it forces every call site into view, where an additive
struct field (right for batches 3/4, where the common case needed to
stay silently unaffected) would have let some existing call sites
silently drift out of sync with the new rule.

Test changes: `mock.h`/`mock.c`'s `rcp_mock_server_transition()`
passthrough gains the same parameter; its 7 test call sites (2
`to_hw_configured()`/`to_rcp_configured()` helper pairs in
`test_mock.c` and `test_tc18_gaps_e2e.c`, `test_conditional_dispatch.c`'s
own `fixture()`) updated -- the bootstrap advance passes `{0}` (correct,
not just convenient, since it's not consulted), the `RCP_CONFIGURED`
advance passes an authorized `root` context. `test_lifecycle.c`'s 9
direct `rcp_lifecycle_transition()` call sites updated individually,
with 2 new dedicated authorization tests added
(`test_transition_hw_configured_to_rcp_configured_requires_authorization`,
covering both the reset-transition siblings). `test_tc18_gaps_server.c`'s
own DEVIATION PIN (`test_transition_takes_neither_idle_nor_writer_input`)
split into `test_transition_takes_no_idle_input` (the still-open
`REQ-LIFECYCLE-022`/`023` EPs_NOT_IDLE gap, unaffected by this batch,
kept as its own pin) and `test_transition_now_rejects_unauthorized_writer`
(the closed half, rewritten to assert the corrected behavior and
`RCP_LIFECYCLE_ERR_UNAUTHORIZED`'s own `strerror()` case rather than
probing a still-unknown error code that this batch's new enum value
would have collided with). `test_discovery_write_authority_survives_
rcp_configured`'s own single transition call attributed to the
discovery stream, matching its own discovery-claim narrative.

Mutation-tested two ways: (1) `git stash` on `src/lifecycle.c` alone
-> build breaks (`conflicting types`), the new-API signature-change
signal; (2) a precise single-line mutation (`bool authorized = true;`,
keeping the signature intact) isolates the authorization logic itself
-> exactly the 4 new/rewritten pinned assertions fail
(`Expected 4 Was 0`), every other test still passes. Both restored
clean. Full suite (64/64) + ASan/UBSan build both clean. Fresh `cfusa
check` (0 errors) + `cfusa trace --req-coverage 100 --sec-tested 100`
(100%/100%). 1029 requirements (unchanged), 137 `tc18-gap` entries
remaining (unchanged count -- `REQ-LIFECYCLE-031` moves
`not-implemented` -> `partial` within the gap category, not a full
closure).

### 158. Phase 5b batch 6: REQ-LIFECYCLE-024 write-denial error response, wire-error-code correction (issue #198)

TC18 §12.3 (Figure 16) requires a denied configuration write be
answered with an error response, not silently dropped.
`rcp_lifecycle_field_writable()` returns only a `bool`, with no wire
error code attached to a `false` verdict at all.

**Primary-source verification caught a real error in this catalog
entry's own prior text before implementing anything, mirroring the
`REQ-LIFECYCLE-028` heading-typo precedent** (this time the error was in
this repository's own generated catalog, not the spec PDF itself): the
pre-existing `REQ-LIFECYCLE-024` text named `RCP_ERROR_LOCKED_MEM_ACCESS`
as the expected wire code, and an existing `test_tc18_gaps_regmap.c`
deviation pin repeated the same assumption in its own comment. Checked
directly against the primary-source PDF (`pdftotext`-extracted, grep'd
for the concrete text rather than trusting either prior citation) and
found TC18 §13.7.1.2's own worked example says otherwise: *"Writing
data to read only registers has no effect and request is confirmed
normally [err=0]. Writing to a write prohibited register (e.g. lock bit
for map set) creates a response with err=1 and an error code
UNAUTHORIZED_ACCESS."* The error-code table itself (§12.9.6) lists both
`UNAUTHORIZED_ACCESS` and `LOCKED_MEM_ACCESS` by name with no
description column filled in for either -- this one worked example is
the *only* concrete guidance TC18 gives for either code anywhere in the
document.

**Corrected understanding, now the implemented fix**: all three of
`rcp_lifecycle_field_kind_t`'s kinds (`HW_GENERIC`/`FUNCTIONAL_W`/
`FUNCTIONAL_W_STAR`) are genuinely writable-in-principle configuration
that becomes write-prohibited under lifecycle-state or writer-
authorization conditions -- none is TC18's distinct "read only by
nature" case (those, e.g. `vendor_id`, sit entirely outside this
writability model already). So *any* denial from
`rcp_lifecycle_field_writable()`, for *any* reason (state alone --
e.g. `HW_GENERIC` past `HW_UNCONFIGURED` -- or writer/frame
authorization on top of an otherwise-permitting state), is TC18's
single "write prohibited register" case: `UNAUTHORIZED_ACCESS`,
uniformly. New `rcp_lifecycle_field_write_error()` (`REQ-WIREERR-004`,
matching `rcp_e2e_wire_error()`'s established `xxx_wire_error()`
mapping-function convention) implements exactly that: `RCP_ERROR_NONE`
when `rcp_lifecycle_field_writable()` is true, `RCP_ERROR_
UNAUTHORIZED_ACCESS` otherwise -- a single `if`, deliberately not a
second, separately-maintained copy of `rcp_lifecycle_field_writable()`'s
own state/kind table (which would risk the two drifting out of sync).
`RCP_ERROR_LOCKED_MEM_ACCESS` is deliberately never emitted: it
corresponds to the *separate*, still entirely unmodeled
`svr_configuration_lock` mechanism (TC18 §12.9's "safety configuration
data can be locked to be read only via the RC Server's configuration",
Table 18 offset 0x0015 -- already tracked by
`test_configuration_lock_register_is_absent`'s own deviation pin, out
of this batch's scope).

Test changes: new `test_field_write_error_maps_every_denial_to_
unauthorized_access` (`test_lifecycle.c`) exercises one case each of
state-only, writer-only, and non-unicast-only denial, all mapping to
the same code, plus a `RCP_ERROR_LOCKED_MEM_ACCESS` negative assertion.
`test_tc18_gaps_regmap.c`'s own `test_write_outcome_has_no_wire_
response_distinction` deviation pin -- which had itself mislabeled
`HW_GENERIC`-past-`HW_UNCONFIGURED` as TC18's distinct "read only"
case rather than a state-locked write-prohibited one -- renamed to
`test_field_write_error_matches_tc18_write_prohibited_example` and
rewritten to assert the corrected, closed behavior directly.

Mutation-tested two ways: (1) `git stash` on `src/lifecycle.c` alone ->
build breaks (`Undefined symbols ... _rcp_lifecycle_field_write_error`),
the new-API signature; (2) a precise single-line mutation (returning
`RCP_ERROR_LOCKED_MEM_ACCESS` instead of `RCP_ERROR_UNAUTHORIZED_ACCESS`)
isolates the mapping logic itself -> exactly the 2 new/rewritten pinned
tests fail (`Expected 3 Was 4`). Both restored clean. Full test suite
(64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) +
`cfusa trace --req-coverage 100 --sec-tested 100` (100%/100%). 1030
requirements (was 1029, `REQ-WIREERR-004` added), 136 `tc18-gap`
entries remaining (was 137, one genuine closure).

### 159. Phase 5b batch 7: REQ-LIFECYCLE-022 EPs_NOT_IDLE gate, Figure 16 primary-source corrections (issue #198)

TC18 Figure 16's extracted text (this project's own pre-existing
`/tmp/tc18_full.txt` OCR/`pdftotext -layout` dump) is genuinely too
garbled to trust for a design decision as consequential as this one --
a state-machine diagram, not prose, does not survive `-layout`
extraction cleanly. Before implementing anything, read the actual PDF
page image directly (page 42) via the PDF-page-image capability rather
than continuing to rely on jumbled extracted text. This produced two
results this batch: a clean, implementable fix (`REQ-LIFECYCLE-022`)
and a corrected, narrower understanding of two already-catalogued gaps
(`REQ-LIFECYCLE-025`/`034`) that turned out not to have a clear
implementable fix at all.

**REQ-LIFECYCLE-022 (implemented, partial)**: the diagram confirms the
`EPs_NOT_IDLE` gate applies to exactly two transitions -- the
`HW_UNCONFIGURED -> HW_CONFIGURED` advance ("...& all other EPs are
Idle -> send positive response") and either reset-to-`HW_UNCONFIGURED`
transition ("...& other EPs are not Idle -> send error response
EPs_NOT_IDLE") -- and explicitly does NOT apply to the `HW_CONFIGURED
-> RCP_CONFIGURED` advance, whose own label makes no mention of
idleness. `rcp_lifecycle_transition()` gains an `all_other_eps_idle`
parameter and a new `RCP_LIFECYCLE_ERR_EPS_NOT_IDLE` error code (value
5, additive), gating exactly those two transitions. Scoped `partial`
rather than `implemented`: Figure 16 names this outcome `EPs_NOT_IDLE`
but that name maps to none of the seventeen numbered wire error codes
`errors.h` models (§12.9.6's own table lists both `UNSUPPORTED_CMD`
through `CHAIN_ERROR` by number -- no `EPs_NOT_IDLE` anywhere) -- a
genuine TC18 inconsistency this library cannot resolve by inventing a
mapping, so the new error code is local-only for now, matching the
`REQ-LIFECYCLE-031`/`RCP_LIFECYCLE_ERR_UNAUTHORIZED` precedent exactly.

**Blast radius, third time using the required-parameter technique**:
same playbook as batch 5 -- a required parameter (not an additive
struct field, since there's no sensible default) forces every call
site into view via the compiler's own "too few arguments" errors. 20
call sites across `src/mock.c`'s one wrapper and 5 test files, all
fixed from the compiler's own enumeration, zero surprise blast radius.
`test_tc18_gaps_server.c`'s own `test_transition_takes_no_idle_input`
deviation pin (from batch 5) rewritten to
`test_transition_now_requires_idle_for_demotion`, asserting the closed
behavior directly; a new dedicated test added for the
`HW_UNCONFIGURED -> HW_CONFIGURED` half
(`test_transition_hw_unconfigured_to_hw_configured_requires_idle`),
since nothing had pinned that direction's gate yet.

**REQ-LIFECYCLE-025/034 (narrowed from `not-implemented` to `partial`,
no code change)**: the same Figure 16 image inspection revealed
`RCP_CONFIGURED`'s own box has NO equivalent "unknown stream/bb_id ->
ignore" transition anywhere in the diagram, unlike `HW_UNCONFIGURED`'s
and `HW_CONFIGURED`'s boxes, which both have one explicitly.
`HW_CONFIGURED`'s own version of this rule is already substantively
satisfied by `REQ-LIFECYCLE-032`'s merged fix (EP0-only admission).
`RCP_CONFIGURED`'s absence of an equivalent rule is genuine spec
silence, not a straightforward c-RCP gap -- inventing an "unknown
association -> drop" behavior for `RCP_CONFIGURED` that TC18's own
diagram does not actually specify would risk introducing new
non-conformant behavior, not fixing existing non-conformance, so no
code change was made. Separately, re-verified `REQ-LIFECYCLE-034`'s own
prose citation (TC18.txt L2458-2459) precisely: "Configuration requests
directly to EPs are only allowed In RCP_CONFIGURED or in HW_CONFIGURED
with valid stream_id/byte_bus_id configuration" -- read exactly, this
means RCP_CONFIGURED direct-to-EP requests are allowed
*unconditionally* (no per-request association check required there at
all), narrower than this requirement's own original text implied; and
HW_CONFIGURED's own half is, again, already conservatively satisfied by
`-032`. Both `.fusa-reqs.json` entries corrected to reflect these
precise findings (their own text is the artifact of record here, per
the standing instruction to keep requirements current with new TC18
discoveries) rather than left stale documenting an over-broad or
imprecise gap.

Mutation-tested two ways: `git stash` on `src/lifecycle.c` alone ->
build breaks (`conflicting types`), the new-API signature-change
signal; a precise mutation removing both `if (!all_other_eps_idle)
return RCP_LIFECYCLE_ERR_EPS_NOT_IDLE;` checks (replaced with a no-op
comment, signature untouched) -> exactly the 2 new/rewritten pinned
tests fail (`Expected 5 Was 0`), every other test still passes. Both
restored clean. Full test suite (64/64) + ASan/UBSan build both clean.
Fresh `cfusa check` (0 errors) + `cfusa trace --req-coverage 100
--sec-tested 100` (100%/100%, all three gates verified as the exact
separate invocations CI uses, per batch 6's own lesson). 1030
requirements (unchanged, no new REQ ids), 136 `tc18-gap` entries
remaining (unchanged count -- three entries move `not-implemented`/
`not-implemented`/`not-implemented` -> `partial`/`partial`/`partial`
within the gap category, no full closures this batch).

### 160. Phase 5b batch 8: REQ-LIFECYCLE-023 + LOCKED_MEM_ACCESS/UNAUTHORIZED_ACCESS correction to batch 6 (issue #198)

Scoping `REQ-LIFECYCLE-023` (endpoint-generic + response-queue
configuration locking) required re-reading Figure 16's own HW_CONFIGURED
box once more, and it carried a transition batch 6 had not checked:
"Request on discovery stream or known stream/bb_id for configuration to
HW_CONFIG or QUEUE_CFG or EP_GEN_CFG -> send error response
LOCKED_CONFIG_ACCESS". This directly contradicts batch 6's own
conclusion (that every `rcp_lifecycle_field_writable()` denial maps to
`RCP_ERROR_UNAUTHORIZED_ACCESS` uniformly, based on §13.7.1.2's prose
alone) -- Figure 16 gives a second, more specific rule the prose alone
did not surface, and batch 6's primary-source check, while genuine, was
incomplete: it verified the prose but not the diagram.

**REQ-LIFECYCLE-023 (implemented, no new code)**: `RCP_LIFECYCLE_FIELD_
HW_GENERIC`'s existing writability rule (writable only in
`HW_UNCONFIGURED`, for any writer) already exactly matches Figure 16's
own HW_CONFIG/QUEUE_CFG/EP_GEN_CFG grouping -- these three blocks share
one identical locking rule and one identical error response in the
diagram, confirming they belong to one behavioral kind, not three (or a
fourth new one). The fix is a documentation clarification only:
`rcp_lifecycle_field_kind_t`'s own doc comment now explicitly states
that `HW_GENERIC` covers the endpoint-generic
(`rcp_regmap_ep_generic_cfg_t`) and response-queue/request-stream
(`rcp_regmap_response_queue_cfg_t`/`rcp_regmap_request_stream_cfg_t`,
where not already covered by Table 22's own separate
`FUNCTIONAL_W_STAR` legend) blocks too, matching this codebase's own
established convention (see `FUNCTIONAL_W` vs. `FUNCTIONAL_W_STAR`) of
adding a distinct enum value only when behavior actually differs, not
mirroring the register map's own struct boundaries one-for-one.

**Correction to already-merged PR #214 (batch 6)**: `rcp_lifecycle_
field_write_error()` (`REQ-WIREERR-004`) restored to its original
two-tier design (comparing the real writer's verdict against a
maximally-privileged writer's verdict for the same state/kind, to
isolate whether a denial is state-driven or writer-driven) -- the exact
design batch 6 initially had, then abandoned as an unnecessary
"simplification" after checking only §13.7.1.2's prose. State-driven
denials (every `HW_GENERIC` denial; `FUNCTIONAL_W_STAR` once
`RCP_CONFIGURED`) now correctly return `RCP_ERROR_LOCKED_MEM_ACCESS` --
the closest numbered wire code to Figure 16's diagram-only
"LOCKED_CONFIG_ACCESS" name, unambiguous by elimination since it is the
only one of the seventeen numbered codes with a semantically matching
name (the same prose-vs-table naming variance already documented for
`POCI_FAILURE`). Writer/frame-driven denials on top of an
otherwise-permitting state still return `RCP_ERROR_UNAUTHORIZED_ACCESS`,
matching §13.7.1.2's own narrower example -- batch 6's conclusion for
that case was correct, just not universal. `REQ-LIFECYCLE-024`'s and
`REQ-WIREERR-004`'s own `.fusa-reqs.json` text corrected a second time
to record both the original mistake and this correction transparently,
rather than silently overwriting the history.

**Reusable lesson**: primary-source verification that checks only prose
and skips an adjacent diagram (or vice versa) is not automatically
complete just because it is genuine -- TC18 states some rules in one
form only, others in both forms with complementary specificity, and a
few (like this one) in both forms describing genuinely different
cases. When a spec section has both prose and a diagram, check both
before concluding a rule is fully understood, not just whichever form
answers the immediate question fastest.

Test changes: `test_lifecycle.c`'s and `test_tc18_gaps_regmap.c`'s own
batch-6 tests (`test_field_write_error_maps_every_denial_to_
unauthorized_access` / `test_field_write_error_matches_tc18_write_
prohibited_example`) both renamed to `test_field_write_error_
distinguishes_state_from_writer_denial` and rewritten to assert the
corrected two-tier behavior, including a `FUNCTIONAL_W_STAR`-once-
`RCP_CONFIGURED` case neither prior version exercised.
`test_tc18_gaps_server.c`'s own `REQ-LIFECYCLE-023` deviation pin
(`test_locked_config_write_has_no_kind_and_no_error_response`, which
used a fabricated out-of-range `rcp_lifecycle_field_kind_t` value to
demonstrate the gap) rewritten to `test_hw_generic_covers_ep_generic_
and_queue_config_with_locked_response`, asserting `HW_GENERIC` directly
covers the closed behavior.

Mutation-tested two ways: a precise single-line mutation (returning
`RCP_ERROR_UNAUTHORIZED_ACCESS` from the state-only branch instead of
`RCP_ERROR_LOCKED_MEM_ACCESS`, signature untouched) -> exactly the 3
rewritten pinned tests fail (`Expected 4 Was 3`) across all three
files; a full-file revert of `src/lifecycle.c` -> build breaks
(unrelated to this specific line, confirms the file is still exercised
end to end). Both restored clean. Full test suite (64/64) + ASan/UBSan
build both clean. Fresh `cfusa check` (0 errors) + `cfusa trace
--req-coverage 100 --sec-tested 100` (100%/100%, all three gates
verified as CI's own separate invocations). 1030 requirements
(unchanged, no new REQ ids), 135 `tc18-gap` entries remaining (was
136, one genuine closure -- `REQ-LIFECYCLE-023`).

### 161. Phase 5b batch 9: REQ-LIFECYCLE-026/035/037 -- discovery-claim binding to the HW_UNCONFIGURED and RCP_CONFIGURED gates (issue #198)

Group 3's discovery-claim binding items. `REQ-LIFECYCLE-026` and
`REQ-LIFECYCLE-035` turned out to be literal duplicates -- both name
the exact same gap (`RCP_LIFECYCLE_FIELD_HW_GENERIC`'s HW_UNCONFIGURED
writability, at two different TC18 citations, §12.3.1.1 and §12.7.2)
and are closed by one fix. `REQ-LIFECYCLE-037` is a structurally
different fix in a different function, batched together since both are
compact, no-signature-change logic corrections in the same "discovery
stream authorization scope" family.

**REQ-LIFECYCLE-026/035 (implemented)**: both entries' own text blamed
`rcp_lifecycle_should_accept()` for not checking the frame's stream_id
against the discovery claimant -- primary-source re-verification found
that was the wrong enforcement point. `should_accept()` is a
frame-admission filter (subtype/msg-type/byte_bus_id only, no op or
stream-identity input); TC18's own write-authorization rules live at
`rcp_lifecycle_field_writable()` in this codebase, the same layering
`REQ-LIFECYCLE-027`/`030`/`036` already established. The real gap:
`RCP_LIFECYCLE_FIELD_HW_GENERIC`'s HW_UNCONFIGURED branch admitted any
writer unconditionally -- `writable = (state ==
RCP_LIFECYCLE_HW_UNCONFIGURED)`, no authorization concept at all, even
though HW_GENERIC models exactly the HW_config block TC18 §12.3.1.1
says "must be run via the stream which was used for discovery." Fixed:
`writable = (state == RCP_LIFECYCLE_HW_UNCONFIGURED) &&
writer.via_discovery_stream` -- no root client or owning stream can
exist yet this early in bring-up, so via_discovery_stream is the only
authorizing condition available. Deriving that bit from a real frame's
stream_id against `rcp_discovery_claim_is_claimant()` (`src/discovery.c`)
remains caller composition, per `discovery.h`'s own documented
architecture ("this module deliberately does not itself decide whether
a given register field is writable... a caller combines [its answer]
with [`rcp_lifecycle_field_writable()`]") -- the identical division of
responsibility `REQ-LIFECYCLE-027`'s `via_non_unicast_frame` member
already established for this library, and already closed at the
library layer under that same convention.

**REQ-LIFECYCLE-037 (implemented)**: this entry's own text also named
the wrong mechanism -- it blamed `rcp_discovery_claim_note_config_write()`
never consulting lifecycle state and `rcp_discovery_claim_release()`
never being called on the HW_CONFIGURED -> RCP_CONFIGURED transition.
Direct code reading found `rcp_lifecycle_field_writable()`'s
`FUNCTIONAL_W` RCP_CONFIGURED branch already correctly excluded
`via_discovery_stream` before this fix (`authorized` there is
`via_root_client_ep0 || via_owning_stream` only) -- field WRITES were
never actually leaking discovery-stream authority into RCP_CONFIGURED.
The real, confirmed gap was in `rcp_lifecycle_transition()`'s own
RCP_CONFIGURED -> HW_UNCONFIGURED reset path: it shared one
`authorized` computation (`via_discovery_stream || via_root_client_ep0`)
with the HW_CONFIGURED -> HW_UNCONFIGURED reset, so a bare
discovery-stream writer could still demote a fully RCP_CONFIGURED
server -- a live pinned test,
`test_transition_rcp_configured_to_hw_unconfigured_is_unconditional_
once_authorized`, asserted exactly this as passing behavior. That is
itself a configuration change TC18 §12.7.4's "Changes in configuration
via a discovery request are no longer allowed" forbids. Fixed: the
RCP_CONFIGURED -> HW_UNCONFIGURED reset now requires
`writer.via_root_client_ep0` specifically; the HW_CONFIGURED ->
HW_UNCONFIGURED reset keeps the wider discovery-stream-or-root-client
rule, since §12.7.3 still permits the discovery stream while
HW_CONFIGURED. `rcp_discovery_claim_note_config_write()`/
`rcp_discovery_claim_release()` remain pure claim-bookkeeping
primitives that deliberately do not consult lifecycle state, per
`discovery.h`'s own documented architecture -- the claim primitive
still refreshes for the claimant regardless of state, but that no
longer translates into actual write or transition authority once
RCP_CONFIGURED, since both real gates now independently close that
door.

**Reusable lesson, third instance this phase**: a `.fusa-reqs.json`
entry's own text naming a specific function as the enforcement point is
not itself evidence that function is where the real gate belongs (or is
even missing one) -- this codebase's established should_accept()
(frame admission) vs. field_writable()/transition() (write/state
authorization) layering means the audit that first wrote these three
entries guessed the wrong layer for all three. Verifying against the
actual code structure, not just the requirement text, caught this
before any of the three fixes landed in the wrong function.

Test changes: `test_lifecycle.c` gained a `discovery` writer to
`test_hw_generic_writable_only_in_hw_unconfigured` and
`test_field_write_error_distinguishes_state_from_writer_denial`, and
`test_transition_rcp_configured_to_hw_unconfigured_is_unconditional_
once_authorized` was renamed
`test_transition_rcp_configured_to_hw_unconfigured_requires_root_client`
and rewritten to assert the corrected rejection. `test_tc18_gaps_
regmap.c` gained a `DISCOVERY_WRITER` constant alongside `ROOT_WRITER`/
`PLAIN_WRITER`, used in `test_general_static_part_has_no_read_only_
class`, `test_hw_config_row_stride_absent_and_access_class_inverted`
and `test_field_write_error_distinguishes_state_from_writer_denial`.
`test_tc18_gaps_server.c`'s `test_hw_generic_covers_ep_generic_and_
queue_config_with_locked_response` gained a discovery writer for its
HW_UNCONFIGURED assertion, and
`test_discovery_write_authority_survives_rcp_configured` was extended
(not replaced) to demonstrate both the still-honest claim-bookkeeping
residual and the two real gates now correctly closing RCP_CONFIGURED
against a bare discovery writer.

Mutation-tested two ways: a full-file revert of `src/lifecycle.c` ->
exactly 3 pinned tests fail
(`test_hw_generic_writable_only_in_hw_unconfigured`,
`test_transition_rcp_configured_to_hw_unconfigured_requires_root_client`,
`test_discovery_write_authority_survives_rcp_configured`), each pinning
a distinct closed requirement; a precise single-line mutation (the
RCP_CONFIGURED-from reset's `!writer.via_root_client_ep0` check swapped
back to `!authorized`) -> exactly the 2 tests pinning `REQ-LIFECYCLE-037`
fail, isolated from the HW_GENERIC fix. Both restored clean. Full test
suite (64/64) + ASan/UBSan build both clean. Fresh `cfusa check` (0
errors) + `cfusa trace --gaps` / `--req-coverage 100` / `--sec-tested
100` (all three separate invocations, matching CI; 100%/100%, 0
untested). 1030 requirements (unchanged, no new REQ ids), 132
`tc18-gap` entries remaining (was 135, three genuine closures).

**Phase 5b progress after batch 9**: 10/16 items fully closed
(`-023`/`-024`/`-026`/`-027`/`-028`/`-030`/`-032`/`-035`/`-036`/`-037`),
4 partial (`-022`/`-025`/`-031`/`-034`). Remaining fully open: `-029`
(deliberately deferred, see `rcp_lifecycle_should_accept()`'s own doc
comment) and `-033` (REQUEST_REJECTED for non-STANDARD requests to EP0
before full configuration -- needs `rcp_lifecycle_should_accept()`'s
return type widened to a 3-state accept/drop/reject result, since ABB
vs. GBB at the wire level already distinguishes STANDARD from every
conditional-request kind without needing a separate `RCP_SCHED_KIND_*`
input as the entry's own text originally assumed -- next batch).

### 162. Phase 5b batch 10: REQ-LIFECYCLE-033 + REQ-LIFECYCLE-029 -- REQUEST_REJECTED for non-STANDARD EP0 requests (issue #198)

The last two fully-open items in Phase 5b, closed together since they
turned out to be the same code path with two conflicting TC18
prescriptions.

**REQ-LIFECYCLE-033 (implemented)**: `RCP_ERROR_REQUEST_REJECTED` (11)
already existed in `errors.h` but was emitted nowhere. The entry's own
text said a new `RCP_SCHED_KIND_*` input was needed to distinguish
STANDARD from conditional requests -- primary-source re-verification
found this unnecessary: this codebase already wire-encodes STANDARD
requests as `ACF_ABB` and every conditional request kind (compound/
compound-wait/triggered/chained/timed/cancel) as `ACF_GBB`
unconditionally, and `rcp_lifecycle_should_accept()` already receives
`acf_msg_type` as a parameter. `rcp_lifecycle_should_accept()`'s `bool`
return widened to a three-way `rcp_lifecycle_accept_t`
(`ACCEPT`/`DROP`/`REJECT`) -- a non-ABB message addressed correctly to
EP0 (`RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID`) now returns `REJECT` in both
`HW_UNCONFIGURED` and `HW_CONFIGURED` (this library's own architecture
makes TC18 §12.7's "no valid stream_id/byte_bus_id combinations have
been defined" condition permanently true throughout `HW_CONFIGURED`,
since no wire-level regmap read/write exists yet to establish one).
`rcp_mock_server_dispatch()` answers a `REJECT` with
`RCP_ERROR_REQUEST_REJECTED`, reusing the pre-existing
`RCP_MOCK_DISPATCH_REJECTED` result value (already used for a different
rejection case, `RCP_SERVER_ADMIT_REJECTED`) -- same semantic, one enum
value, no new mock-layer machinery needed.

**REQ-LIFECYCLE-029 (implemented, resolved differently than
originally scoped)**: this entry's own remaining residual after
`REQ-LIFECYCLE-032`'s fix -- a conditional (`ACF_GBB`) request literally
addressed to EP0 in `HW_CONFIGURED` -- turned out to be the exact same
case `REQ-LIFECYCLE-033` governs, and TC18's two sections directly
conflict for it: §12.3.1.2 says ACF_GBB requests are "dropped without
further response"; §12.7 says EP0-addressed non-STANDARD requests get
`REQUEST_REJECTED`. Reconciled by reading §12.3.1.2 as the general,
non-EP0-scoped rule (still correctly enforced -- any non-EP0
`byte_bus_id` drops unconditionally of `acf_msg_type`, via
`REQ-LIFECYCLE-032`'s own restriction) and §12.7 as the more specific,
EP0-scoped override that governs here -- the same specific-overrides-
general reading TC18 uses elsewhere (e.g. Table 22's own `W*` legend
narrowing the general `W` rule). `.fusa-reqs.json` text for both entries
records this cross-reference explicitly.

**New standing lesson**: two catalogued requirements can name the exact
same code path with prescriptions that directly contradict each other
when read in isolation -- resolving this needs comparing BOTH entries'
primary-source citations side by side, not implementing either in
isolation. Caught here specifically because scoping `-033` required
re-reading `rcp_lifecycle_should_accept()`'s own existing `-029`
deviation-pin comment, which already flagged the exact same EP0-GBB
case as its own tracked residual.

Test changes: `test_lifecycle.c` gained
`test_hw_unconfigured_rejects_non_abb_message_type_addressed_to_ep0`
and `test_hw_configured_rejects_non_abb_message_type_addressed_to_ep0`;
every existing `TEST_ASSERT_TRUE`/`FALSE(rcp_lifecycle_should_accept(...))`
call site across `test_lifecycle.c` and `test_tc18_gaps_server.c`
rewritten to `TEST_ASSERT_EQUAL` against the specific
`RCP_LIFECYCLE_ACCEPT`/`DROP`/`REJECT` value (implicit bool truthiness
would otherwise silently invert every one of these assertions, since
`RCP_LIFECYCLE_ACCEPT == 0` is C-false). `test_tc18_gaps_server.c`'s
`test_hw_unconfigured_ignores_claimant_and_request_kind` renamed
`test_hw_unconfigured_admission_ignores_claimant_but_writes_still_gated`
and rewritten to demonstrate should_accept()'s frame-admission layer
correctly not checking stream identity (that is `field_writable()`'s
job, per `REQ-LIFECYCLE-026`/`035`'s own batch) alongside the new
REJECT behavior; its own `test_hw_configured_admits_gbb_pending_
lifecycle_029` renamed `test_hw_configured_rejects_gbb_addressed_to_ep0`
and rewritten. `test_mock.c` gained
`test_dispatch_rejected_by_lifecycle_sends_request_rejected_error`,
decoding the actual wire response mock.c now builds.

Mutation-tested two ways: a full revert of `src/lifecycle.c`,
`src/mock.c` and `include/rcp/lifecycle.h` together breaks the BUILD
(test files reference `RCP_LIFECYCLE_ACCEPT`/`DROP`/`REJECT`, undefined
without the header change) -- stronger confirmation than a test failure
alone, the same class of signal prior signature-changing batches (5, 7)
produced; a precise single-line mutation (both `? RCP_LIFECYCLE_ACCEPT
: RCP_LIFECYCLE_REJECT` ternaries replaced with an unconditional
`RCP_LIFECYCLE_ACCEPT`) -> exactly the 4 tests pinning `REJECT`
specifically fail, isolated from every other assertion. Both restored
clean. Full test suite (64/64) + ASan/UBSan build both clean. Fresh
`cfusa check` (0 errors) + `cfusa trace --gaps` / `--req-coverage 100`
/ `--sec-tested 100` (all three separate invocations, matching CI;
100%/100%, 0 untested). 1030 requirements (unchanged, no new REQ ids),
130 `tc18-gap` entries remaining (was 132, two genuine closures).

**Phase 5b progress after batch 10**: 12/16 items fully closed
(`-023`/`-024`/`-026`/`-027`/`-028`/`-029`/`-030`/`-032`/`-033`/`-035`/
`-036`/`-037`), 4 partial (`-022`/`-025`/`-031`/`-034`) -- the phase's
full 16-item scope is now accounted for, all fixable-given-this-
library's-architecture items closed and every remaining item honestly
scoped `partial`/`tc18-gap` with its own architecture-limit citation.
Phase 5b (issue #198) is effectively complete; Phase 5c (issue #199,
PWRMODE sleep/wake handshake gaps) is the next phase.

### 163. Phase 5c batch 1: REQ-PWRMODE-019 -- wake-handshake completion actually re-enables endpoints (issue #199)

First batch of the new phase: 15 requirements across 4 groups (issue
#199). Started with Group 1's own highest-severity item, flagged in the
issue itself: "a woken server reports a completed hot start while
every endpoint stays disabled."

**REQ-PWRMODE-019 (partial)**: `rcp_pwrmode_handshake_resume_queues()`
(`power.h`) advances only a state enum, by design -- that module's own
file header states it never touches `server.h` at all ("driving the
actual re-init sequence... remains a caller's job"), the identical
"pure primitive, caller composes" layering `lifecycle.h`/`discovery.h`
already established and this session has repeatedly relied on (most
recently for `REQ-LIFECYCLE-026`/`035`). The real gap was that NO
caller anywhere in this codebase ever performed that composition.
Fixed: `mock.h` gains `rcp_mock_server_pwrmode_resume(srv, hs)` --
calls `rcp_pwrmode_handshake_resume_queues(hs)` first, then (iff it
succeeds) calls `rcp_server_endpoint_set_enable()` on every registered
endpoint slot, mirroring the pre-existing per-endpoint
`rcp_mock_server_set_endpoint_enable()` but for all of them at once.
Kept `status: "partial"`, not fully closed: response-queue objects
(`rcp_regmap_response_queue_cfg_t` is inert -- no `STREAM_UID`/
`queue_size`/storage) and heartbeat-stream re-emission have no
implementation anywhere in this codebase yet (both already-tracked,
separate architecture gaps this fix cannot close -- see
`test_response_queue_has_no_identity_size_or_storage()` and
`test_flush_triggers_and_heartbeat_are_absent()`, `tests/test_tc18_
gaps_regmap.c`).

**Primary-source verification**: read TC18 §12.4.1 (Power-On/Wake-Up/
Start-Up behavior) directly. Confirmed the exact quoted text: "After
reception of valid message from the sleep request Client all used
endpoints and response queues will be enabled." Also surfaced, but
deliberately NOT acted on this batch: the section's OPENING sentence
classifies "wake-up from sleep" as a COLD start and "wake-up from
StandBy" as a HOT start, which appears to contradict the detailed
"Hot-start-up procedure" text immediately following it (network
re-enable, WakeUp-message repetition) -- a procedure that, by its own
physical-layer details (re-enabling the network interface, matching
Sleep's own "only part of the network PHY... powered" characteristic),
reads more like a Sleep-wake procedure than a StandBy-wake one, despite
being filed under "hot-start." This looks like a genuine internal TC18
terminology inconsistency, not a c-RCP gap -- flagged here rather than
resolved, since issue #199 itself does not name it as a tracked item
and the existing `rcp_pwrmode_wake_from_sleep()`/handshake design
(REQ-PWRMODE-005 through -012, all already `IMPLEMENTED`) is built on
the detailed-procedure reading. Re-architecting that foundational
classification is out of scope for a single batch and needs explicit
scoping of its own before any change -- noted here so it isn't lost.

Test changes: `test_tc18_gaps_server.c`'s own
`test_wake_completion_reenables_nothing_and_network_skips_all` split in
two -- the REQ-PWRMODE-019 half moved to `test_mock.c` (where
`rcp_mock_server_pwrmode_resume()` naturally lives) as two new tests,
`test_pwrmode_resume_reenables_all_endpoints` and
`test_pwrmode_resume_returns_false_before_handshake_echoed`; the
remaining `REQ-PWRMODE-020` half stays in `test_tc18_gaps_server.c`,
renamed `test_network_wake_skips_handshake_entirely` (still an open
deviation pin -- not fixed this batch).

Mutation-tested two ways: a full revert of `src/mock.c`/`include/rcp/
mock.h` alone (test files unchanged) breaks the BUILD (`test_mock.c`
references the new function, undeclared without it); a precise
single-line mutation (the `rcp_server_endpoint_set_enable()` call
replaced with a no-op) isolates exactly the one test pinning the real
enable behavior. Both restored clean. Full test suite (64/64) +
ASan/UBSan build both clean. Fresh `cfusa check` (0 errors) + `cfusa
trace --gaps` / `--req-coverage 100` / `--sec-tested 100` (all three
separate invocations, matching CI; 100%/100%, 0 untested). 1030
requirements (unchanged, no new REQ ids), 130 `tc18-gap` entries
remaining (unchanged count -- `-019` moved `not-implemented`-equivalent
text to a more precisely-scoped `partial`, not a full closure).

**Phase 5c progress after batch 1**: 1/15 items addressed (`-019`,
partial). **Next**: `REQ-PWRMODE-020` (network-wake handshake skip,
already has a fresh deviation pin from this batch), then the rest of
Group 1 (`-016`/`-017`/`-018`), per issue #199's own suggested order.
