# Threat Analysis and Risk Assessment (TARA)
## ISO 21434 Clause 9 — c-RCP v0.57.0
Generated: 2026-07-28T01:05:37Z

**Hand-authored, not `cfusa tara`-generated.** `cfusa tara` only emits a
placeholder skeleton — it has no input-file mechanism to seed real
content, so any release regenerating this file would silently overwrite
hand-authored content back to boilerplate. This file is instead
maintained by hand and no longer regenerated on release, matching the
precedent already set by `HARA.md`/`.fusa-hara.json` in this same repo
(see `ROADMAP.md` milestone 57 and `CYBERSECURITY.md` §3). Update by
hand when threats, mitigations, or their implementation status change —
do not run `cfusa tara` over this file.

---

## 1. Asset Identification

| Asset | Description | Security Property |
|---|---|---|
| ASSET-001 | RCP command/control channel between HPC callers and zone controllers (`rcp_controller_send`/`loan`) | Integrity, Availability |
| ASSET-002 | RCP status/telemetry stream from zone controllers to subscribers (`rcp_controller_subscribe`) | Integrity |
| ASSET-003 | Zone controller registry and registration identity (`rcp_registry_register`/`dial`) | Integrity |
| ASSET-004 | OTA firmware image and update/rollback session state (`rcp_firmware_session_t`) | Integrity |

## 2. Threat Scenarios

| ID | Asset | Threat | Attack Vector | Attacker Profile |
|---|---|---|---|---|
| TS-001 | ASSET-001 | Command injection / spoofed HPC: forged commands injected by an entity impersonating a legitimate HPC caller | remote | Attacker with existing network/bus access to the HPC-zone command path |
| TS-002 | ASSET-002 | Replay attack: a previously-valid command or status message is captured and replayed to cause unintended repeated actuation or a stale status report | remote | Attacker able to passively capture and rapidly replay traffic within the anti-replay window |
| TS-003 | ASSET-003 | Rogue zone controller registration: an attacker registers a controller for a zone it does not own, hijacking that zone's command routing and status reporting | remote or physical | Attacker with network access to the zone bus/registry; no valid certificate required given the current TLS-stub posture (see §5, TS-003) |
| TS-004 | ASSET-004 | OTA firmware tampering: an attacker tampers with or substitutes a delivered firmware image, or forces an unauthorized rollback to a vulnerable version | remote | Attacker able to intercept or substitute the OTA delivery path |
| TS-005 | ASSET-001 | Command-flood DoS: an attacker floods a zone controller with commands to exhaust processing capacity and starve legitimate commands | remote | Attacker with the ability to generate sustained command traffic |

## 3. Impact Assessment

| Threat | Safety Impact | Financial Impact | Operational Impact | Privacy Impact |
|---|---|---|---|---|
| TS-001 | high — unauthorized actuation of zone-controlled functions (ASIL-B scoped per HARA.md) | not quantified (library-level SEOOC; deployment-specific) | zone-dependent, potentially immediate | none (no PII in the RCP data path) |
| TS-002 | high — repeated unintended actuation from replayed commands | not quantified | zone-dependent | none |
| TS-003 | severe — full command-routing and status hijack for the affected zone | not quantified | zone-dependent, potentially sustained | none |
| TS-004 | severe — compromised firmware compromises the entire zone controller | not quantified | sustained until re-flash/recovery | none |
| TS-005 | moderate — RCP_PRIORITY_CRITICAL commands are exempted by design, bounding safety impact | not quantified | temporary, non-critical commands only | none |

## 4. Attack Feasibility

| Threat | Elapsed Time | Expertise | Knowledge | Equipment | Windows |
|---|---|---|---|---|---|
| TS-001 | 1wk | proficient | restricted | standard | moderate |
| TS-002 | 1mo | expert | restricted | specialised | difficult |
| TS-003 | 1wk | proficient | restricted | standard | moderate |
| TS-004 | 1wk | proficient | restricted | standard | moderate |
| TS-005 | 1wk | proficient | restricted | specialised | moderate |

_TS-005 note: naively sending a lot of traffic is trivial (layman/public/easy), but the token-bucket limiter's `exempt_critical` default means **achieving actual denial of a safety-relevant function** — the threat's real damage scenario — requires materially more sophistication than simple flooding. The ratings above describe the effort to achieve that outcome, not to merely transmit packets._

## 5. Risk Determination

| Threat | Impact | Feasibility | Risk Value | Risk Level |
|---|---|---|---|---|
| TS-001 | 3 | 3 | 9 | HIGH |
| TS-002 | 3 | 2 | 6 | MEDIUM |
| TS-003 | 4 | 3 | 12 | HIGH |
| TS-004 | 4 | 3 | 12 | HIGH |
| TS-005 | 2 | 2 | 4 | LOW |

## 6. Risk Treatment

| Threat | Treatment | Cybersecurity Goal | Requirement Ref |
|---|---|---|---|
| TS-001 | reduce | No command shall be forwarded to a zone controller unless the caller's asserted identity is permitted for that zone/command-type by the loaded policy. | REQ-AUTH-001, REQ-AUTH-002 |
| TS-002 | reduce | No command or status message carrying a previously-seen or excessively-stale sequence number shall be accepted. | REQ-E2E-007, REQ-E2E-008 |
| TS-003 | reduce | Only a controller presenting a valid, CA-verified certificate matching its claimed zone shall be permitted to register or dial into the registry. | REQ-TLS-001, REQ-TLS-002 |
| TS-004 | reduce (partial) | Only a firmware image matching an expected, cryptographically-verified content hash shall be activated. **Currently unmet — tracked as issue #69.** | REQ-FW-005 |
| TS-005 | reduce | A flood of non-critical commands shall not exhaust a zone controller's processing capacity; `RCP_PRIORITY_CRITICAL` commands shall remain unaffected. | REQ-RL-001, REQ-RL-004 |

### Notes on residual risk (read before citing this document as evidence of full mitigation)

- **TS-001 / TS-003** (the two HIGH-risk entries tied to identity/authentication): `rcp_authz_controller_new()` (Layer 2) and the TLS integration surface (Layer 1, `tls.h`) are real, but `tls.c` is a **confirmed compile-time stub** absent an OpenSSL/wolfSSL/mbedTLS backend — every transport call returns `RCP_ERR_NOT_SUPPORTED` rather than an insecure fallback (secure-by-refusal, not secure-by-encryption). This library's own default posture (mock/sim/udp/shmem transports) has **no working cryptographic identity source**; closing this residual risk requires an integrator to supply a real TLS backend, consistent with `CYBERSECURITY.md`'s own SEOOC framing of Layer 1.
- **TS-002**: this risk rating is based on `rcp_e2e_replay_guard_t`'s tested runtime behavior (`test_e2e.c`), not on a formal-proof claim. `FORMAL_VERIFICATION.md`'s claim that `tla/AntiReplayGuard.tla` model-checks cleanly is separately disputed and under remediation (issue #57); this entry does not rely on it.
- **TS-004**: `rcp_firmware_session_verify()` is a protocol-flow state-machine gate only — **no cryptographic image-hash or signature verification exists** (confirmed by direct source read of `src/firmware.c`, filed as issue #69). `CYBERSECURITY.md` previously claimed a SHA-256 check existed here; that claim has been corrected (2026-07-28) to avoid the exact TARA/CYBERSECURITY.md inconsistency this document exists to prevent.
- **TS-005**: the only threat in this TARA with a genuinely complete, working mitigation at the library level with no external backend dependency.

---
_Document owner: SoundMatt/c-RCP maintainers_
_Review date: on next threat model change or annually, whichever is sooner_
_Standard: ISO 21434:2021 Clause 9_
