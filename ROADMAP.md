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
| **Fragmentation** | v0.76.0 | Fragmentation (GO) | Multi-AVTPDU `ms`/`segment_num` support for CAN XL, UART, large discovery reads |
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

### 59. AVTPDU framing (v0.59.0)

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

### 60. ACF message format + byte_message_info header (v0.60.0)

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

---
### Phase 14 — RC Server Lifecycle & Register Map
---

Spec basis: Ch.12 (extraction §3). The gap analysis flags the 3-state
lifecycle as "probably the single biggest structural gap" relative to the
old informal protocol — expect this to touch register-access code paths
pervasively for the rest of the program.

### 61. Lifecycle state machine (v0.61.0)

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

### 62. Register-map model (v0.62.0)

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

---
### Phase 15 — Discovery
---

Spec basis: extraction §3.5. Flagged as "newly clarified" and tightly
wound into lifecycle bootstrapping — the second-highest-impact structural
gap after Phase 14 per the gap analysis's own prioritization (extraction
§9 item 2).

### 63. Discovery (v0.63.0)

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

### 64. GPIO endpoint (v0.64.0)

- `include/rcp/ep_gpio.h` + `src/ep_gpio.c`: up to 32 pins, 4-byte
  bitmask request/response payload, all 8 `evt[2:0]` write-semantics
  (replace/OR/AND/XOR/add/subtract, with add/subtract saturating rather
  than wrapping at `0x0000`/`0xFFFF`, plus the reconfiguration escape
  hatch at `evt[2:0]=7`, extraction §4.5 Group C).
- Per-pin trigger signals (any-change/rising/falling) and functional
  config (direction, electrical properties mirroring the HW pin-mapping
  table's pull-up/down/drive-strength fields for runtime adjustment).

### 65. SPI endpoint (v0.65.0)

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

### 66. I²C + UART endpoints (v0.66.0)

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

### 67. ADC + PWM_OUT + PWM_IN endpoints (v0.67.0)

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

---
### Phase 17 — Conditional Requests & Sequencers
---

Spec basis: extraction §2.7, §3.11, §3.14, §3.16. Flagged as likely
wholesale-new territory relative to the old informal protocol (extraction
§6 items 6–7) — if the old protocol only ever did simple immediate
read/write, this entire subsystem has no precedent in this codebase to
port from.

### 68. Compound + compound-wait requests + sequencers (v0.68.0)

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

### 69. Triggered/chained/timed requests + cancellation taxonomy (v0.69.0)

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

---
### Phase 18 — E2E Protection (Safe Points)
---

Spec basis: extraction §4.7, §3.8. Flagged as "probably the most
safety-critical gap area" (extraction §6 item 4) — this is the direct,
explicit replacement for `e2e.c`'s ad-hoc CRC-16 mechanism named in this
program's own charter.

### 70. Safe points: CRC32 + safety-request variants (v0.70.0)

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

### 71. LIN commander endpoint (v0.71.0)

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

### 72. CAN controller endpoint, incl. CAN XL (v0.72.0)

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

### 73. ISELED endpoint (v0.73.0)

- `include/rcp/ep_iseled.h` + `src/ep_iseled.c`: native 4-bit/5-bit ISELED
  framing over ISP_P/ISP_N, client payload taken as raw plain instruction/
  address/data content and ISELED-encoded by the endpoint; optional
  native ISELED CRC (separate from and additional to Phase 18's RCP-level
  CRC32); recovered-clock mode (`iseled_use_rcv_clk`) needing no ISP_N pin
  wired at all; single transmission-complete trigger.

### 74. MDIO endpoint (v0.74.0)

- `include/rcp/ep_mdio.h` + `src/ep_mdio.c`: the endpoint entirely absent
  from the spec's own informative "ten interfaces" scope list yet fully
  specified in its normative register map (extraction §1.2) — Clause-22
  MMD / Clause-45 MMS single-word and burst addressing modes, no
  type-specific functional config beyond the universal common block, no
  trigger-signal table. Reusable to expose an on-die integrated PHY's
  management registers when no physical MDIO pins are mapped at all.

### 75. Wakeup control endpoint + power modes (v0.75.0)

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

### 76. Fragmentation support (v0.76.0)

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

---
### Phase 21 — Satellite Package Rework
---

Works back through every entry in the Satellite Package Disposition table
above, now that Phases 13–20 have landed a stable new core API to rebind
against. Grouped into batches the same way the legacy program's own
"coverage maximization" milestones (v0.50.0–v0.54.0) were batched, for the
same reason: each batch is independently shippable and CI-green on its
own, rather than one enormous PR.

### 77. Foundational test/config satellites (v0.77.0)

- **REPLACE** `mock.c`: in-process RC-Server/endpoint test double
  replacing the old zone-controller mock, built against the Phase 14
  register-map model and Phase 16+ endpoint types directly (no
  Command/Response shape left to double for).
- **REPLACE** `config.c`: RC-Server/endpoint manifest loader (HW pin map,
  EP list, stream config) replacing the old zone-manifest JSON schema.
- **ADAPT** `cli.c`: `capabilities`'s payload content moves from the old
  `RCP_CMD_*` enum to the new `svr_implemented_options` feature-bundle
  flags; `version`/`status` structurally unaffected.

### 78. Transport satellites (v0.78.0)

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

### 79. Safety-adjacent satellites (v0.79.0)

- **REPLACE** `watchdog.c`: thin client convenience around the Phase 18
  per-stream `rx_wd_*` registers and safety-request sequences, not an
  independent `RCP_CMD_WATCHDOG` side channel.
- **REPLACE** `deadline.c`: liveness re-derived from response/ack-queue
  `Flush_time` heartbeats and/or `rx_wd_info_enable` notifications — no
  generic "Status stream" left to monitor.
- **REPLACE** `powerstate.c`: client convenience wrapper around the
  Phase 19 Wakeup endpoint + Normal/StandBy/Sleep/Unpowered model,
  replacing the ad-hoc Active/Sleeping/BusOff state machine entirely.

### 80. Generic decorators, batch 1 (v0.80.0)

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

### 81. Protocol bridges (v0.81.0)

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

### 82. Optional discovery convenience (v0.82.0)

- **ADAPT** `mdns.c`: retained strictly as an optional convenience
  discovery layer scoped to the IEEE1722-over-UDP/IP transport variant,
  layered beside — never instead of — the Phase 15 native broadcast
  discovery mechanism, which remains the only mandatory discovery path.

### 83. Deprecation batch (v0.83.0)

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

### 84. RELAY adapter rework (v0.84.0)

- **REPLACE** `relay/relay.h` + `adapt.c`: RELAY's generic
  `Message`/`ToMessage()`/`FromMessage()` envelope assumes one generic
  command/response shape; TC18's 13 heterogeneous, fixed-shape endpoint
  types don't map onto it without per-endpoint-type translation rules
  RELAY itself doesn't yet define. This milestone proposes an interim
  per-endpoint-type message mapping scoped to this repo, and opens the
  upstream conversation with `SoundMatt/RELAY` (a GitHub issue against
  that repo, not an edit to it — per this project's own cross-repo
  policy) rather than assuming a resolution unilaterally.

---
### Phase 22 — Safety/Security Re-certification
---

Deliberately last, per the Satellite Disposition table's reasoning:
re-deriving certification evidence from code that doesn't exist yet is
backwards. Spec basis for the *content* being re-derived: whatever Phases
13–21 actually built, not the superseded catalog.

### 85. Full re-certification pass (v0.85.0)

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
