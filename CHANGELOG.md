# Changelog

All notable changes to c-RCP are recorded here, one entry per tagged
release, newest first. Generated retroactively (issue #108) from
`ROADMAP.md`'s existing per-milestone detail and this repository's own
tag history; maintained going forward as a release-gate requirement
alongside `ROADMAP.md`, per RELAY spec §19.2.

## Deprecation & Removal Log

Per RELAY spec §19.2, this table records every module deprecation:
the version it was deprecated in, its replacement (if any), and the
version it was actually removed in. Both entries below deprecated and
removed a module in the *same* release, with no intervening MINOR
deprecation window -- see each module's own release entry below for
the rationale.

| Module | Deprecated in | Replacement | Removed in |
|---|---|---|---|
| `tls.h`/`tls.c` | v0.78.0 | MACsec (802.1AE, link-layer; out of this library's scope) | v0.78.0 |
| `prioqueue.h`/`prioqueue.c` | v0.83.0 | Protocol-defined request-kind execution priority (server-side) | v0.83.0 |
| `firmware.h`/`firmware.c` | v0.83.0 | None (OTA is explicitly an OEM/application-layer concern, out of RCP's scope) | v0.83.0 |
| `zonegroup.h`/`zonegroup.c` | v0.83.0 | None (no multi-target broadcast grouping concept in TC18) | v0.83.0 |
| `proxy.h`/`proxy.c` | v0.83.0 | None (multi-hop/bridging is a network concern, not RCP's) | v0.83.0 |
| `redundancy.h`/`redundancy.c` | v0.83.0 | None (no server-redundancy concept in TC18) | v0.83.0 |
| `federation.h`/`federation.c` | v0.83.0 | None (no TC18 counterpart to the zone/HPC-lease registry model) | v0.83.0 |
| `dyndata.h`/`dyndata.c` | v0.83.0 | None (every TC18 endpoint has one fixed, spec-defined payload shape) | v0.83.0 |
| `wire.h`/`wire.c` (the `'RC'`-magic placeholder codec) | v0.91.0 | The real TC18 AVTP/ACF layer (`avtp.h`/`avtp.c`, `acf.h`/`acf.c`), present and primary since Phase 13 | v0.91.0 |
| `sim.h`/`sim.c` (zone-controller simulator) | v0.91.0 | None in this repo; SiL/HIL simulation against the TC18 RC Server/Endpoint model can be built on `mock.h`/`mock.c` instead | v0.91.0 |
| `rcp.h`/`rcp.c`'s pre-TC18 object model (`rcp_zone_t`, `rcp_command_t`, `rcp_response_t`, `rcp_status_t`, `rcp_controller_t`, `rcp_registry_t`) | v0.91.0 | The TC18 register-map/lifecycle/endpoint core (`regmap.h`, `lifecycle.h`, `ep_*.h`) | v0.91.0 |

## Releases

### v0.94.0 -- 2026-07-30

Fix three CLI `capabilities` self-report accuracy issues found by the
2026-07-30 ecosystem audit. Dropped `tls` from `transports`: `tls.h`/
`tls.c` were removed outright at v0.78.0 (see this file's Deprecation &
Removal Log), so the self-report was advertising a transport backend
that no longer exists in the tree at all. `RCP_CLI_ALL_IMPLEMENTED_
OPTIONS` (a single hardcoded "every bit in all three groups" constant,
despite its own comment claiming it named "one per group this build
actually implements") is now `RCP_CLI_IMPLEMENTED_OPTIONS`, computed
from three independently-named per-group predicates. Independently
verified against the actual code that none of the three feature groups
(`time_sync`, `enhanced_cancel`, `compound_bundles`) is fully
TC18-wire-conformant yet, despite each having real, working API surface
compiled in (`request_timed.c`'s `presentation_time` is a plain
`uint32_t`; `request_cancel.c`'s encoders hard-code the ACF header's
`evt` sub-field to 0; `request_compound.c`'s `repeat_count` is an
8-bit, round-tripped-only sub-field with no repetition state machine)
-- documented this "API surface, not wire conformance" caveat directly
in `cli.c` and `README.md`, since the RELAY capabilities JSON schema's
`features` array (`additionalProperties: false`, flat `string[]`) has
no way to carry a per-entry caveat on the wire itself. (Milestone 94;
see `ROADMAP.md` for full detail.)

### v0.93.0 -- 2026-07-30

Add the numbered TC18 wire error-code table (`rcp/errors.h`,
`rcp_wire_error_t`), which this project previously shipped no
representation of at all -- only local `rcp_errc_t`/module-local
`rcp_<mod>_errc_t` enums, none of which model the numbered codes an RC
Server places in a Response frame's err field. Also fixes `src/e2e.c`'s
`rcp_e2e_strerror()`, which hardcoded the spec's own prose alias
"CRC_ERROR" for a CRC mismatch -- a name with no entry in the spec's
authoritative numbered table, which instead assigns a CRC mismatch the
code POCI_FAILURE (12). Added `rcp_e2e_wire_error()`, mapping
`RCP_E2E_ERR_CRC_MISMATCH` to `RCP_ERROR_POCI_FAILURE` (and everything
else to a new `RCP_ERROR_NONE` sentinel for "no applicable wire code").
(Milestone 93; see `ROADMAP.md` for full detail.)

### v0.92.0 -- 2026-07-30

BREAKING: fix two E2E CRC32 wire-conformance defects found by the
2026-07-30 ecosystem audit. `rcp_e2e_compute_crc()` (and the
`rcp_e2e_wrap()`/`rcp_e2e_unwrap()` pair built on it) folded
`avtp_timestamp` into the CRC as 8 octets; it is a 4-octet field on the
wire (matching `avtp.h`'s own `uint32_t` modeling of it), so every
E2E-protected frame carried 4 extra always-zero octets no conformant
peer would include -- no safe-command-mode CRC produced by this
implementation could validate against one, or vice versa. Changed the
`avtp_timestamp` parameter on all three functions from `uint64_t` to
`uint32_t`.

Separately, `rcp_e2e_wrap()` never performed the mandated "adapt
acf_msg_length by plus one quadlet before computing the CRC" step:
it appended the trailer to an already-encoded ACF frame without
touching the acf_msg_length field baked into that frame's header, so
the declared message length under-counted the trailer by 4 octets --
wrong for any peer relying on acf_msg_length to find message
boundaries in a concatenated ACF stream. `rcp_e2e_wrap()` now adapts a
copy of the given frame's acf_msg_length (offset 1-2 per acf.h's
documented layout) by +1 quadlet before computing the CRC and
appending the trailer; `rcp_e2e_unwrap()` reverses both steps and
returns an owned copy (previously a borrowed pointer into the input)
ready to hand to acf.c's decoders unmodified.

(Milestone 92; see `ROADMAP.md` for full detail.)

### v0.91.0 -- 2026-07-30

BREAKING: remove the retired pre-TC18 placeholder protocol residue --
`wire.h`/`wire.c` (the `'RC'`-magic codec), `sim.h`/`sim.c` (the
zone-controller simulator), the `rcp_zone_t`/`rcp_command_t`/
`rcp_response_t`/`rcp_status_t`/`rcp_controller_t`/`rcp_registry_t`
object model in `rcp.h`/`rcp.c`, the `tests/legacy_mock.*` double and its
six placeholder-certifying test/benchmark targets, ~90 dead
`REQ-ZONE`/`REQ-PRI`/`REQ-CMD`/`REQ-STATUS`/`REQ-CMDSTRUCT`/`REQ-RESP`/
`REQ-STAT`/`REQ-CTRL`/`REQ-REG`/`REQ-SIM`/`REQ-ERR-*` requirement
entries, and the orphaned `tooling/zone_manifest_schema.json`. Per RELAY
spec §15.5 this ships with no compatibility shim. `rcp.h`/`rcp.c` retain
only the protocol-agnostic primitives every TC18 module actually shares
(`rcp_bytes_t`, the base `rcp_errc_t` sentinels, `rcp_context_t`,
`RCP_SPEC_VERSION`) -- reworked as new `REQ-CORE-*` requirements, since
their old `REQ-CTRL-026`/`REQ-CTRL-027`/`REQ-STAT-004` tags described
retired-model-specific copy semantics. Also fixes two generic-primitive
requirement families (mutex/condvar/thread in `platform.c`, the
monotonic clock in `clock.c`) that had been coincidentally tagged with
soon-to-be-deleted `REQ-CTRL-*` IDs despite implementing unrelated,
codebase-wide infrastructure -- retagged as new `REQ-PLATFORM-*`
requirements with dedicated test coverage (`tests/test_platform.c`).
(Milestone 91; see `ROADMAP.md` for full detail.)

### v0.90.0 -- 2026-07-29

Add CHANGELOG.md. (Milestone 90; see `ROADMAP.md` for full detail.)

### v0.89.0 -- 2026-07-29

RELAY_SPEC_VERSION tracks spec v2.0. (Milestone 89; see `ROADMAP.md` for full detail.)

### v0.88.0 -- 2026-07-29

cfusa-trace CI job now gates on --sec-tested too. (Milestone 88; see `ROADMAP.md` for full detail.)

### v0.87.0 -- 2026-07-29

Pin c-FuSa to v0.5.49. (Milestone 87; see `ROADMAP.md` for full detail.)

### v0.86.0 -- 2026-07-29

GPIO/PWM_OUT write-semantics enum off-by-one. (Milestone 86; see `ROADMAP.md` for full detail.)

### v0.85.0 -- 2026-07-28

Full re-certification pass. (Milestone 85; see `ROADMAP.md` for full detail.)

### v0.84.0 -- 2026-07-28

RELAY adapter rework. (Milestone 84; see `ROADMAP.md` for full detail.)

### v0.83.0 -- 2026-07-28

Deprecation batch. (Milestone 83; see `ROADMAP.md` for full detail.)

### v0.82.0 -- 2026-07-28

Optional discovery convenience. (Milestone 82; see `ROADMAP.md` for full detail.)

### v0.81.0 -- 2026-07-28

Protocol bridges. (Milestone 81; see `ROADMAP.md` for full detail.)

### v0.80.0 -- 2026-07-28

Generic decorators, batch 1. (Milestone 80; see `ROADMAP.md` for full detail.)

### v0.79.0 -- 2026-07-28

Safety-adjacent satellites. (Milestone 79; see `ROADMAP.md` for full detail.)

### v0.78.0 -- 2026-07-28

Transport satellites. (Milestone 78; see `ROADMAP.md` for full detail.)

### v0.77.0 -- 2026-07-28

Foundational test/config satellites. (Milestone 77; see `ROADMAP.md` for full detail.)

### v0.76.0 -- 2026-07-28

Fragmentation support. (Milestone 76; see `ROADMAP.md` for full detail.)

### v0.75.0 -- 2026-07-28

Wakeup control endpoint + power modes. (Milestone 75; see `ROADMAP.md` for full detail.)

### v0.74.0 -- 2026-07-28

MDIO endpoint. (Milestone 74; see `ROADMAP.md` for full detail.)

### v0.73.0 -- 2026-07-28

ISELED endpoint. (Milestone 73; see `ROADMAP.md` for full detail.)

### v0.72.0 -- 2026-07-28

CAN controller endpoint, incl. CAN XL. (Milestone 72; see `ROADMAP.md` for full detail.)

### v0.71.0 -- 2026-07-28

LIN commander endpoint. (Milestone 71; see `ROADMAP.md` for full detail.)

### v0.70.0 -- 2026-07-28

Safe points: CRC32 + safety-request variants. (Milestone 70; see `ROADMAP.md` for full detail.)

### v0.69.0 -- 2026-07-28

Triggered/chained/timed requests + cancellation taxonomy. (Milestone 69; see `ROADMAP.md` for full detail.)

### v0.68.0 -- 2026-07-28

Compound + compound-wait requests + sequencers. (Milestone 68; see `ROADMAP.md` for full detail.)

### v0.67.0 -- 2026-07-28

ADC + PWM_OUT + PWM_IN endpoints. (Milestone 67; see `ROADMAP.md` for full detail.)

### v0.66.0 -- 2026-07-28

I²C + UART endpoints. (Milestone 66; see `ROADMAP.md` for full detail.)

### v0.65.0 -- 2026-07-28

SPI endpoint. (Milestone 65; see `ROADMAP.md` for full detail.)

### v0.64.0 -- 2026-07-28

GPIO endpoint. (Milestone 64; see `ROADMAP.md` for full detail.)

### v0.63.0 -- 2026-07-28

Discovery. (Milestone 63; see `ROADMAP.md` for full detail.)

### v0.62.0 -- 2026-07-28

Register-map model. (Milestone 62; see `ROADMAP.md` for full detail.)

### v0.61.0 -- 2026-07-28

Lifecycle state machine. (Milestone 61; see `ROADMAP.md` for full detail.)

### v0.60.0 -- 2026-07-28

ACF message format + byte_message_info header. (Milestone 60; see `ROADMAP.md` for full detail.)

### v0.59.0 -- 2026-07-28

AVTPDU framing. (Milestone 59; see `ROADMAP.md` for full detail.)

### v0.58.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 5: real TLC verification. (Milestone 58; see `ROADMAP.md` for full detail.)

### v0.57.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 4: real TARA content. (Milestone 57; see `ROADMAP.md` for full detail.)

### v0.56.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 3: Adapt() error wrapping. (Milestone 56; see `ROADMAP.md` for full detail.)

### v0.55.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 2: mechanical fixes. (Milestone 55; see `ROADMAP.md` for full detail.)

### v0.54.0 -- 2026-07-27

Coverage maximization, batch 4: internal helpers. (Milestone 54; see `ROADMAP.md` for full detail.)

### v0.53.0 -- 2026-07-27

Coverage maximization, batch 3: registry lifecycle. (Milestone 53; see `ROADMAP.md` for full detail.)

### v0.52.0 -- 2026-07-27

Coverage maximization, batch 2: decorator vtable methods. (Milestone 52; see `ROADMAP.md` for full detail.)

### v0.51.0 -- 2026-07-27

Fix: branch coverage still not captured on real CI. (Milestone 51; see `ROADMAP.md` for full detail.)

### v0.50.0 -- 2026-07-27

Coverage maximization, batch 1: branch instrumentation + strerror() functions. (Milestone 50; see `ROADMAP.md` for full detail.)

### v0.49.0 -- 2026-07-27

CI hardening: remove vestigial CFUSA-L004 escape hatch. (Milestone 49; see `ROADMAP.md` for full detail.)

### v0.48.0 -- 2026-07-27

`relay conform` CI gate -- closes #12. (Milestone 48; see `ROADMAP.md` for full detail.)

### v0.47.0 -- 2026-07-27

CLI binary — `version`/`capabilities`/`status` -- closes #8. (Milestone 47; see `ROADMAP.md` for full detail.)

### v0.46.0 -- 2026-07-27

`Adapt()`/`ToMessage()`/`FromMessage()`/`SpecVersion` -- closes #10. (Milestone 46; see `ROADMAP.md` for full detail.)

### v0.45.0 -- 2026-07-27

`rcp_zone_string()` PascalCase + `rcp_zone_from_string()` -- closes #11. (Milestone 45; see `ROADMAP.md` for full detail.)

### v0.44.0 -- 2026-07-27

Wire decoder integer-overflow fix -- closes #9. (Milestone 44; see `ROADMAP.md` for full detail.)

### v0.43.0 -- 2026-07-27

Certification. (Milestone 43; see `ROADMAP.md` for full detail.)

### v0.42.0 -- 2026-07-27

ISO 21434 / Cybersecurity. (Milestone 42; see `ROADMAP.md` for full detail.)

### v0.41.0 -- 2026-07-27

Formal Verification. (Milestone 41; see `ROADMAP.md` for full detail.)

### v0.40.0 -- 2026-07-27

RTOS / Bare-Metal. (Milestone 40; see `ROADMAP.md` for full detail.)

### v0.39.0 -- 2026-07-27

DoIP Bridge. (Milestone 39; see `ROADMAP.md` for full detail.)

### v0.38.0 -- 2026-07-27

UDS Bridge. (Milestone 38; see `ROADMAP.md` for full detail.)

### v0.37.0 -- 2026-07-27

LIN Bridge. (Milestone 37; see `ROADMAP.md` for full detail.)

### v0.36.0 -- 2026-07-27

MQTT Bridge. (Milestone 36; see `ROADMAP.md` for full detail.)

### v0.35.0 -- 2026-07-27

DDS Bridge. (Milestone 35; see `ROADMAP.md` for full detail.)

### v0.34.0 -- 2026-07-27

CAN Bridge. (Milestone 34; see `ROADMAP.md` for full detail.)

### v0.33.0 -- 2026-07-27

SOME/IP Bridge. (Milestone 33; see `ROADMAP.md` for full detail.)

### v0.32.0 -- 2026-07-27

REST Bridge. (Milestone 32; see `ROADMAP.md` for full detail.)

### v0.31.0 -- 2026-07-27

gRPC Bridge. (Milestone 31; see `ROADMAP.md` for full detail.)

### v0.30.0 -- 2026-07-27

Dynamic Data. (Milestone 30; see `ROADMAP.md` for full detail.)

### v0.29.0 -- 2026-07-27

Code Generation. (Milestone 29; see `ROADMAP.md` for full detail.)

### v0.28.0 -- 2026-07-27

Config. (Milestone 28; see `ROADMAP.md` for full detail.)

### v0.27.0 -- 2026-07-27

Record & Replay. (Milestone 27; see `ROADMAP.md` for full detail.)

### v0.26.0 -- 2026-07-27

Admin API. (Milestone 26; see `ROADMAP.md` for full detail.)

### v0.25.0 -- 2026-07-27

Observability. (Milestone 25; see `ROADMAP.md` for full detail.)

### v0.24.0 -- 2026-07-27

Multi-HPC Federation. (Milestone 24; see `ROADMAP.md` for full detail.)

### v0.23.0 -- 2026-07-27

Redundancy. (Milestone 23; see `ROADMAP.md` for full detail.)

### v0.22.0 -- 2026-07-27

Zone Proxy. (Milestone 22; see `ROADMAP.md` for full detail.)

### v0.21.0 -- 2026-07-27

Zone Groups. (Milestone 21; see `ROADMAP.md` for full detail.)

### v0.20.0 -- 2026-07-27

Firmware Update / OTA. (Milestone 20; see `ROADMAP.md` for full detail.)

### v0.19.0 -- 2026-07-27

Authorization. (Milestone 19; see `ROADMAP.md` for full detail.)

### v0.18.0 -- 2026-07-27

Fault Injection. (Milestone 18; see `ROADMAP.md` for full detail.)

### v0.17.0 -- 2026-07-27

Zone Simulator. (Milestone 17; see `ROADMAP.md` for full detail.)

### v0.16.0 -- 2026-07-27

Rate Limiting. (Milestone 16; see `ROADMAP.md` for full detail.)

### v0.15.0 -- 2026-07-27

Priority Queuing. (Milestone 15; see `ROADMAP.md` for full detail.)

### v0.14.0 -- 2026-07-27

E2E Protection. (Milestone 14; see `ROADMAP.md` for full detail.)

### v0.13.0 -- 2026-07-27

Power State. (Milestone 13; see `ROADMAP.md` for full detail.)

### v0.12.0 -- 2026-07-27

Deadline Monitoring. (Milestone 12; see `ROADMAP.md` for full detail.)

### v0.11.0 -- 2026-07-27

Watchdog & Heartbeat. (Milestone 11; see `ROADMAP.md` for full detail.)

### v0.10.0 -- 2026-07-27

TSN Transport. (Milestone 10; see `ROADMAP.md` for full detail.)

### v0.9.0 -- 2026-07-27

Loaned Samples. (Milestone 9; see `ROADMAP.md` for full detail.)

### v0.8.0 -- 2026-07-27

Shared Memory Transport. (Milestone 8; see `ROADMAP.md` for full detail.)

### v0.7.0 -- 2026-07-27

TLS Transport. (Milestone 7; see `ROADMAP.md` for full detail.)

### v0.6.0 -- 2026-07-27

mDNS Discovery. (Milestone 6; see `ROADMAP.md` for full detail.)

### v0.5.0 -- 2026-07-27

UDP Transport. (Milestone 5; see `ROADMAP.md` for full detail.)

### v0.4.0 -- 2026-07-27

HARA Expansion. (Milestone 4; see `ROADMAP.md` for full detail.)

### v0.3.0 -- 2026-07-27

Hardening. (Milestone 3; see `ROADMAP.md` for full detail.)

### v0.2.0 -- 2026-07-27

Requirements. (Milestone 2; see `ROADMAP.md` for full detail.)

### v0.1.0 -- 2026-07-27

Foundation. (Milestone 1; see `ROADMAP.md` for full detail.)

