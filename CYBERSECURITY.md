# Cybersecurity Architecture — c-RCP (Milestone 42)
## IEC 62443 SL-2 / ISO 21434

**Document version**: 1.0.0

---

## 1. Security Layers

### Layer 1 — Transport Security (TLS 1.3)

`include/rcp/tls.h` provides the integration surface for mTLS. The actual
TLS implementation (OpenSSL / wolfSSL / mbedTLS) is plugged in at the
application layer.

- Mutual certificate authentication (both HPC and zone controller present certs)
- Certificate CN is extracted and passed to `rcp_authz_controller_new()`
- Cipher suites: TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384 (mandatory)

### Layer 2 — Command Authorization (`rcp_authz_controller_new()`)

Each `send()` is checked against an `rcp_authz_policy_t` table (bitmask-based
zone/command-type entries). The policy is loaded from a signed manifest at
boot. `RCP_ERR_FORBIDDEN` is returned without forwarding the command.

REQ-AUTH-001..REQ-AUTH-008

### Layer 3 — E2E Anti-Replay (`rcp_e2e_replay_guard_t`)

A 32-entry sliding-window bitmap (`RCP_E2E_REPLAY_WINDOW_SIZE`) detects
replayed sequence numbers. Sequence numbers more than 32 behind the
high-water mark are unconditionally rejected. A TLA+ model of this guard
exists at `tla/AntiReplayGuard.tla` (see `FORMAL_VERIFICATION.md`), but
its claimed clean model-check is currently disputed and under
remediation — see issue #57. This mechanism's guarantee is currently
substantiated by its runtime test suite (`test_e2e.c`), not by formal
proof.

REQ-E2E-004, REQ-E2E-005, REQ-E2E-006

### Layer 4 — Rate Limiting (`rcp_ratelimit_controller_new()`)

Token-bucket rate limiter prevents DoS via command flooding.
`RCP_PRIORITY_CRITICAL` commands are exempt by default
(`rcp_ratelimit_config_t.exempt_critical`) to preserve safety function
availability.

REQ-RL-003, REQ-RL-004

### Layer 5 — Firmware Transfer State Machine (`rcp_firmware_session_t`)

`rcp_firmware_session_verify()` is a protocol-flow gate: it advances the
session from `Initiated` to `Verified` (bounded by
`cfg.verify_timeout_ms`) before `activate()` is permitted, preventing an
incomplete or out-of-order transfer from being activated. **Correction
(2026-07-28, issue #69):** this document previously claimed a SHA-256
image-hash check is performed here — confirmed by direct source read
that no cryptographic hash/digest verification exists anywhere in
`src/firmware.c`. `rcp_firmware_session_rollback()` is likewise a plain
command like any other on the wrapped controller, not a specially
authenticated operation in its own right — it inherits whatever
authorization/transport security (Layers 1–2) the deployment layers
around the underlying controller, the same as every other command.
Real cryptographic image-integrity verification is tracked as a gap in
issue #69, not yet implemented.

REQ-FW-005..REQ-FW-007

---

## 2. IEC 62443 SL-2 Gap Analysis

| Requirement | Status | Notes |
|------------|--------|-------|
| FR1 Identification & Authentication | Implemented | mTLS + authz |
| FR2 Use Control | Implemented | `rcp_authz_policy_t` per zone/command-type |
| FR3 System Integrity | Implemented | E2E CRC-16 + anti-replay |
| FR4 Data Confidentiality | Partial | TLS stub; HSM key storage external |
| FR5 Restricted Data Flow | Implemented | Zone isolation in routing |
| FR6 Timely Response | Implemented | Deadline monitor + watchdog |
| FR7 Resource Availability | Implemented | Token-bucket rate limiter |

Machine-readable gap report: `iec62443-gap-report.json`, regenerated on
every tagged release by `cfusa iec62443 --sl SL-2` (see `release.yml`).
Gaps recorded as `GAP (M)` are mandatory CRs with no automated cfusa rule
mapped to them yet (e.g. session integrity, audit log accessibility,
network segmentation) — these require deployment-level controls
(network architecture, logging infrastructure) outside what a
single-process C library can enforce on its own, matching cpp-RCP's own
disposition of the same gaps.

---

## 3. Threat Analysis and Risk Assessment

See `tara.md` / `tara.json` (hand-authored; **no longer** auto-generated
by `cfusa tara` as of 2026-07-28 — see issue #56 and `ROADMAP.md`
milestone 57. `cfusa tara` only emits a placeholder skeleton with no way
to seed real content, so regenerating it on every release silently
clobbered the real TARA back to boilerplate) for the structured TARA
covering command injection, replay attacks, rogue zone controller
registration, OTA firmware tampering, and denial-of-service via command
flooding. Each finding maps
to an implemented countermeasure from the security layers above:

| Threat | Countermeasure |
|---|---|
| Command injection / spoofed HPC | mTLS (Layer 1) + authz (Layer 2) |
| Replay attack | E2E anti-replay guard (Layer 3); formal-verification claim in `FORMAL_VERIFICATION.md` currently disputed, see issue #57 |
| Rogue zone controller registration | mTLS mutual auth (Layer 1) |
| OTA firmware tampering | Partially mitigated: transfer state-machine gate (Layer 5) + whatever Layer 1–2 auth wraps the session; no cryptographic image-hash check yet (tracked: issue #69) |
| Command-flood DoS | Token-bucket rate limiter (Layer 4) |

---

## 4. Relationship to cpp-RCP

This document ports cpp-RCP's `CYBERSECURITY.md` structure and threat
coverage, updated to reference c-RCP's own C module/function names in
place of cpp-RCP's C++ class names. The security architecture itself is
unchanged: c-RCP implements the identical layered defense (mTLS →
authorization → anti-replay → rate limiting → firmware integrity) with
the identical countermeasure-to-threat mapping, since both projects
target the same IEC 62443 SL-2 profile against the same RELAY protocol
surface.
