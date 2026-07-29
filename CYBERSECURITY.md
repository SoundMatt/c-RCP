# Cybersecurity Architecture — c-RCP (Milestone 85)
## IEC 62443 SL-2 / ISO 21434

**Document version**: 2.0.0

This document fully replaces its pre-TC18 (v1.0.0) content, which
described a TLS/anti-replay/firmware-transfer layered defense that no
longer matches this codebase — `tls.c` was deprecated at v0.78.0
(MACsec, not TLS, is the spec's own link-layer security control),
`firmware.c` was removed outright at v0.83.0 (no TC18 endpoint or
message exists for OTA firmware transfer), and the retired CRC-16
anti-replay guard has no TC18 replacement (see §1.3 below). See
`ROADMAP.md` milestone 85 (Phase 22 re-certification pass).

---

## 1. Security Layers

### 1.1 Layer 1 — Link-Layer Authentication (MACsec)

The TC18 spec's own security control is MACsec (IEEE 802.1AE),
explicitly a product-specific/opaque configuration block operating below
this library's own transport code. **Not implemented within this
library** — `include/rcp/tls.h`/`tls.c`, the pre-TC18 application-level
mTLS integration surface, was deprecated at v0.78.0 rather than adapted,
since MACsec is a materially different mechanism (link-layer, not an
application-level session wrapper) with no natural home in the TC18
model. This library's own shipped transports (`udp.c`, `avtp.c`,
`shmem.c`) carry AVTPDUs with no authentication or encryption of their
own; an integrator deploying this library must supply MACsec at the link
layer. See `tara.md` TS-004 for the residual-risk analysis.

### 1.2 Layer 2 — Request Authorization (`rcp_authz_policy_permit()`)

Each request is checked against an `rcp_authz_policy_t` table
(identity/address/request-type entries, keyed on `(stream_id,
byte_bus_id)` rather than the retired zone/command-type pair). A denied
combination is rejected before the request is forwarded to its
endpoint-specific encode/send call. The caller identity is still a
short string label (certificate CN or pre-shared key label); full
certificate-chain validation remains the responsibility of whichever
Layer 1 control is in effect.

REQ-AUTH-001..REQ-AUTH-008

### 1.3 Layer 3 — E2E Safe Points and Safety-Request Execution Gating (`e2e.c`)

CRC32 (poly `0xF4ACFB13`) frame integrity, safety-tagged (MSB-set)
request execution gating on the endpoint's configured safe state, and a
per-stream watchdog whose overflow purge always keeps safety-tagged
requests queued rather than discarding them. Formally verified via TLC
model checking in `tla/E2ESafePoint.tla` (see `FORMAL_VERIFICATION.md`).

**This layer does not include replay/staleness detection.** The
pre-TC18 architecture's "Layer 3 — E2E Anti-Replay" (a 32-entry
sliding-window bitmap over CRC-16-protected sequence numbers) has no
TC18 counterpart in this codebase — confirmed by direct read of
`include/rcp/e2e.h`'s own file header, which records this as a
deliberate scope boundary of milestone 70, not an oversight. See
`tara.md` TS-002 for the residual-risk analysis; no requirement in
`.fusa-reqs.json` claims replay mitigation.

REQ-E2E-011, REQ-E2E-012, REQ-E2E-014, REQ-E2E-015, REQ-E2E-020..REQ-E2E-027

### 1.4 Layer 4 — Discovery/Bootstrap Claim (`discovery.c`)

A first-claimant-wins model with a configurable timeout:
`rcp_discovery_claim_note_request()` grants an open (unheld or lapsed)
claim to the first requester and never preempts an already-active
claimant; `rcp_discovery_claim_note_config_write()` rejects a
configuration write from a non-claimant without mutating state. This
prevents a *second* attacker from displacing an already-bonded
legitimate claimant during a server's `HW_UNCONFIGURED` bootstrap
window, but does not itself cryptographically authenticate the *first*
claimant to arrive — that gap is closed only by Layer 1 (MACsec), not
by this layer alone. See `tara.md` TS-003.

REQ-DISC-015..REQ-DISC-022

### 1.5 Layer 5 — Register-Map Write Authorization (`regmap.c`)

`rcp_regmap_writer_ctx()` grants write authority only to the EP0
root-client (once a root client is established) or to a request
stream's own owning stream. `HW_GENERIC` fields become read-only once
`HW_CONFIGURED`, and `FUNCTIONAL_W_STAR` fields are permanently locked
for the remainder of the configured session once `RCP_CONFIGURED` — the
latter formally verified via `tla/LifecycleStateMachine.tla`'s
`FieldLockMonotonicWhileConfigured` property.

REQ-RMAP-009..REQ-RMAP-012, REQ-LIFECYCLE-018..REQ-LIFECYCLE-020

### 1.6 Layer 6 — Rate Limiting (`ratelimit.c`)

Per-`(stream_id, byte_bus_id)` token-bucket admission control prevents
DoS via request flooding. Safety-tagged (MSB-set) requests are exempt
by default — re-anchored on `rcp_e2e_is_safety_request()` now that the
retired `RCP_PRIORITY_CRITICAL` client-assigned tag has no TC18
counterpart (request execution priority is now a protocol-defined,
server-side property of request kind).

REQ-RL-003, REQ-RL-004

---

## 2. IEC 62443 SL-2 Gap Analysis

| Requirement | Status | Notes |
|------------|--------|-------|
| FR1 Identification & Authentication | Partial | Layer 2 policy check implemented; Layer 1 (MACsec) is a deployment-level dependency, not implemented in this library |
| FR2 Use Control | Implemented | `rcp_authz_policy_t` per identity/address/request-type; `rcp_regmap_writer_ctx()` per register field |
| FR3 System Integrity | Partial | E2E CRC32 safe points implemented; no replay/staleness detection (§1.3) |
| FR4 Data Confidentiality | Not implemented in-library | MACsec is the spec's own control; out of this library's scope (§1.1) |
| FR5 Restricted Data Flow | Implemented | `(stream_id, byte_bus_id)` addressing scopes every request to its endpoint |
| FR6 Timely Response | Implemented | Per-stream watchdog (`rcp_e2e_wd_evaluate()`) + WakeUp handshake completion gate |
| FR7 Resource Availability | Implemented | Per-endpoint token-bucket rate limiter |

Machine-readable gap report: `iec62443-gap-report.json`, regenerated on
every tagged release by `cfusa iec62443 --sl SL-2` (see `release.yml`).
Gaps recorded as `GAP (M)` are mandatory CRs with no automated cfusa
rule mapped to them yet (e.g. session integrity, audit log
accessibility, network segmentation) — these require deployment-level
controls (network architecture, logging infrastructure, MACsec key
management) outside what a single-process C library can enforce on its
own.

---

## 3. Threat Analysis and Risk Assessment

See `tara.md` / `tara.json` (hand-authored; **not** auto-generated by
`cfusa tara` — see issue #56 and `ROADMAP.md` milestone 57. `cfusa tara`
only emits a placeholder skeleton with no way to seed real content, so
regenerating it on every release would silently clobber the real TARA
back to boilerplate) for the structured TARA covering request
injection/spoofing, replay attacks, rogue bootstrap claims, link-layer
eavesdrop/tamper, and denial-of-service via request flooding. Each
finding maps to an implemented (or, where noted, deployment-level)
countermeasure from the security layers above:

| Threat | Countermeasure |
|---|---|
| Request injection / spoofing | Layer 2 (`rcp_authz_policy_permit()`); full identity assurance depends on Layer 1 (MACsec), not implemented in-library |
| Replay attack | **None implemented.** See §1.3 and `tara.md` TS-002. |
| Rogue bootstrap claim | Layer 4 (`discovery.c` first-claimant-wins) + Layer 5 (`regmap.c` writer authorization); full identity assurance depends on Layer 1 |
| Link-layer eavesdrop/tamper | **None implemented in-library.** MACsec is a deployment-level dependency (§1.1). |
| Request-flood DoS | Layer 6 (token-bucket rate limiter with safety-tagged exemption) |

---

## 4. Relationship to Earlier Milestones

This document's v1.0.0 (Milestone 42) ported cpp-RCP's own
`CYBERSECURITY.md` structure and threat coverage, describing the
pre-TC18 Zone/Command wire model's TLS/anti-replay/rate-limiting/
firmware-transfer defense-in-depth. As of Phase 13 (`ROADMAP.md`'s
Protocol Replacement Notice), c-RCP stopped mirroring cpp-RCP
port-for-port; this v2.0.0 revision is derived directly from the actual
TC18 attack surface Phases 13–21 implemented, not ported from any
sibling project.
