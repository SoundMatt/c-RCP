# Threat Analysis and Risk Assessment (TARA)
## ISO 21434 Clause 9 — c-RCP v0.85.0
Generated: 2026-07-28T21:00:00Z

**Hand-authored, not `cfusa tara`-generated.** `cfusa tara` only emits a
placeholder skeleton — it has no input-file mechanism to seed real
content, so any release regenerating this file would silently overwrite
hand-authored content back to boilerplate. This file is instead
maintained by hand and no longer regenerated on release, matching the
precedent already set by `HARA.md`/`.fusa-hara.json` in this same repo
(see `ROADMAP.md` milestone 57 and `CYBERSECURITY.md` §3). Update by
hand when threats, mitigations, or their implementation status change —
do not run `cfusa tara` over this file.

**Phase 22 re-derivation (milestone 85).** This TARA fully replaces the
pre-TC18 threat model — the command/status channel, zone controller
registry, and OTA firmware transfer scenarios it previously covered no
longer describe this codebase (`rcp_zone_t`/`rcp_controller_t` were
retired at Phase 13–14; `firmware.h`/`firmware.c` were DEPRECATE-removed
outright at milestone 83, with no TC18 counterpart to re-analyze). The
threats below are re-derived against the actual TC18 attack surface:
request/response streams, the discovery/lifecycle bootstrap sequence,
and MACsec's role now that `tls.c` is deprecated (`ROADMAP.md`'s
Satellite Package Disposition table).

---

## 1. Asset Identification

| Asset | Description | Security Property |
|---|---|---|
| ASSET-001 | TC18 request/response traffic on a request stream (register-map writes, per-endpoint-type requests) addressed by `(stream_id, byte_bus_id)` | Integrity, Availability |
| ASSET-002 | Response/ack queue and status/telemetry data returned to the caller over a stream's response queue | Integrity |
| ASSET-003 | Discovery/lifecycle bootstrap sequence — EP0 root-client grant, discovery claim, and the HW/RCP configuration manifest applied while `HW_UNCONFIGURED` | Integrity, Availability |
| ASSET-004 | Link-layer transport carrying AVTPDUs (native Ethernet, IEEE1722-over-UDP/IP, or CAN(FD/XL)-as-network) | Confidentiality, Integrity |

## 2. Threat Scenarios

| ID | Asset | Threat | Attack Vector | Attacker Profile |
|---|---|---|---|---|
| TS-001 | ASSET-001 | Request injection / spoofing: forged ACF requests injected by an entity impersonating a legitimate caller identity | remote | Attacker with existing network/bus access to a request stream |
| TS-002 | ASSET-001 | Replay attack: a previously-valid request is captured and replayed to cause unintended repeated actuation or a stale response | remote | Attacker able to passively capture and replay traffic on a request stream |
| TS-003 | ASSET-003 | Rogue bootstrap claim: an attacker claims the EP0 root-client grant during discovery, or writes HW/RCP configuration it does not legitimately own, hijacking the server's configuration | remote or physical | Attacker with network access to the discovery `byte_bus_id` during a server's `HW_UNCONFIGURED` window |
| TS-004 | ASSET-004 | Link-layer eavesdrop/tamper: an attacker with link-layer access reads or modifies AVTPDU traffic in the absence of MACsec | physical | Attacker able to reach the physical/logical link carrying RCP traffic |
| TS-005 | ASSET-001 | Request-flood DoS: an attacker floods an endpoint with requests to exhaust its finite queue capacity and starve legitimate traffic | remote | Attacker with the ability to generate sustained request traffic |

## 3. Impact Assessment

| Threat | Safety Impact | Financial Impact | Operational Impact | Privacy Impact |
|---|---|---|---|---|
| TS-001 | high — unauthorized actuation via a forged request (ASIL-A scoped per HARA.md H-002/H-006) | not quantified (library-level SEOOC; deployment-specific) | endpoint-dependent, potentially immediate | none (no PII in the RCP data path) |
| TS-002 | high — repeated unintended actuation from a replayed request; a library-level mitigation now exists but is opt-in (HARA.md H-004, see §6 note) | not quantified | endpoint-dependent | none |
| TS-003 | severe — full configuration hijack for the affected server during bootstrap (HARA.md H-011) | not quantified | sustained until the next full reset | none |
| TS-004 | severe — undermines confidentiality and integrity of every request on the affected link (HARA.md H-007) | not quantified | sustained until MACsec is deployed | none |
| TS-005 | moderate — safety-tagged requests are exempted by design, bounding safety impact | not quantified | temporary, non-safety requests only | none |

## 4. Attack Feasibility

| Threat | Elapsed Time | Expertise | Knowledge | Equipment | Windows |
|---|---|---|---|---|---|
| TS-001 | 1wk | proficient | restricted | standard | moderate |
| TS-002 | 1d | proficient | public | standard | easy |
| TS-003 | 1wk | proficient | restricted | standard | moderate |
| TS-004 | 1wk | proficient | restricted | specialised | moderate |
| TS-005 | 1wk | proficient | restricted | specialised | moderate |

_TS-002 note: elapsed time/knowledge are rated easier here than the
pre-TC18 TARA's own TS-002 entry, which had a real, tested, TLC-verified
anti-replay guard (the retired `AntiReplayGuard.tla`/`rcp_e2e_replay_guard_t`)
standing in the way. This rating reflects the default/naive posture — a
deployment that has not enabled the opt-in TC18 Table 24
`rx_enforce_seq`/`rx_seq_safestate_enable` mitigation now available (see
§6's notes, updated 2026-08-20) — i.e. capture-and-resend against no
active defense, the same posture this rating described before that
mitigation existed. A deployment that has enabled it and replicated
`mock.c`'s reference wiring in its own dispatch loop faces a materially
higher-effort attack (bounded by the mechanism's own documented residual
risk: per-restart tracker reset, the RFC 1982 `[1,127]` forward-window
limit, and per-AVTPDU-frame granularity), but this table's own numeric
ratings are not re-derived per-deployment-configuration — they describe
this library's shipped default._

_TS-005 note: naively sending a lot of traffic is trivial (layman/public/
easy), but the token-bucket limiter's default safety-tagged exemption
means **achieving actual denial of a safety-relevant function** — the
threat's real damage scenario — requires materially more sophistication
than simple flooding. The ratings above describe the effort to achieve
that outcome, not to merely transmit packets._

## 5. Risk Determination

| Threat | Impact | Feasibility | Risk Value | Risk Level |
|---|---|---|---|---|
| TS-001 | 3 | 3 | 9 | HIGH |
| TS-002 | 3 | 3 | 9 | HIGH |
| TS-003 | 4 | 3 | 12 | HIGH |
| TS-004 | 4 | 3 | 12 | HIGH |
| TS-005 | 2 | 2 | 4 | LOW |

## 6. Risk Treatment

| Threat | Treatment | Cybersecurity Goal | Requirement Ref |
|---|---|---|---|
| TS-001 | reduce | No request shall be forwarded to an endpoint unless the caller's asserted identity is permitted for that endpoint/request-type by the loaded policy. | REQ-AUTH-001, REQ-AUTH-002 |
| TS-002 | reduce | No request carrying a previously-seen sequence number shall be accepted. **Mitigated (opt-in) as of 2026-08-20 (issue #606/#601) — see HARA.md H-004/SG-004 and the note below for scope.** | REQ-E2E-028, REQ-E2E-029 |
| TS-003 | reduce | Only a caller holding a valid, unexpired discovery claim (or, post-bootstrap, an authorized EP0 root-client/owning-stream writer context) shall be permitted to write HW/RCP configuration. | REQ-DISC-017, REQ-DISC-018, REQ-DISC-019, REQ-DISC-021, REQ-RMAP-009..012 |
| TS-004 | reduce | Link-layer authentication (MACsec, 802.1AE) shall be enforced by the deployment on any transport carrying safety-relevant requests. **Deployment-level control; not implemented within this library — see HARA.md H-007/SG-007.** | none (deployment-level) |
| TS-005 | reduce | A flood of non-safety-tagged requests shall not exhaust an endpoint's processing capacity; safety-tagged requests shall remain exempt by default. | REQ-RL-003, REQ-RL-004 |

### Notes on residual risk (read before citing this document as evidence of full mitigation)

- **TS-001**: `rcp_authz_policy_permit()` (Layer 2, REAL and working, exercised by `test_authz.c`) rejects any identity/address/request-type combination not in the policy table. Its identity input is still a caller-supplied short string label (certificate CN or pre-shared key label, per `authz.h`'s own file header); full certificate-chain validation remains the responsibility of whichever link-layer security control (MACsec) is in effect — this library's own default posture (mock/sim/udp/shmem transports) supplies no cryptographic identity source of its own. This is the same residual structure the pre-TC18 TARA's TS-001 entry described for `tls.c`, now re-anchored on MACsec instead of TLS since `tls.c` itself is deprecated (see TS-004).
- **TS-002**: **Mitigated (opt-in), updated 2026-08-20 (issue #606/#601).** The prior "open, unmitigated" note above described a real gap through issue #338 (v0.322.0): the retired CRC-16 sequence-counter/replay-window guard had no TC18 counterpart, and the TC18 CRC32 safe-point mechanism that replaced it is an integrity check, not a freshness check. That gap is now closed at the mechanism level — TC18 §12.7.7 Table 24 itself defines `rx_enforce_seq`/`rx_seq_safestate_enable` (0x000D bits 1/2), and `rcp_e2e_seq_evaluate()`/`rcp_e2e_seq_tracker_t` implement it (`.fusa-reqs.json` REQ-E2E-028/REQ-E2E-029, both `status: "implemented"`, `scope: "tc18"` — a real spec mechanism, not a c-RCP-specific extension), wired into `mock.c`'s reference dispatch composition (`frame_seq_gate_admits()`, once per AVTPDU frame). This is **opt-in**, not automatic: an integrator must enable the config bits via `regmap.h` and replicate `mock.c`'s admission wiring in their own real, I/O-attached dispatch loop (this library ships no networked dispatcher of its own). Residual risk even when enabled: `rcp_e2e_seq_tracker_t` state is per-process/per-restart (no persistence — a replay immediately after a restart is not detected), RFC 1982 forward-window comparison only distinguishes a gap in `[1,127]` from ordinary 8-bit wraparound, and detection is per-AVTPDU-frame, not per-ACF-message. See `include/rcp/e2e.h`'s own file header for the full scope statement. Feasibility/impact above are left unchanged from the pre-mitigation rating since Section 5's Risk Determination reflects the threat absent treatment, matching this document's own convention for every other `reduce`-treated threat in this table; the mitigation's actual effect is captured in the Risk Treatment table and this note, not by re-rating feasibility.
- **TS-003**: `rcp_discovery_claim_note_request()`'s first-claimant-wins model (REAL, working, exercised by `test_discovery.c`) prevents a *second* attacker from displacing an already-bonded legitimate claimant, and `rcp_regmap_writer_ctx()` continues to gate configuration writes by grant afterward — but neither cryptographically authenticates the *first* claimant to arrive during a server's `HW_UNCONFIGURED` bootstrap window. Closing that gap requires the same link-layer authentication TS-004 already tracks as absent.
- **TS-004**: **Open by design, out of this library's own scope.** The TC18 spec's own security control is MACsec — an explicitly product-specific/opaque link-layer configuration block (`ROADMAP.md`'s `tls.h`/`tls.c` DEPRECATE disposition) — not something a portable C99 protocol library implements itself. This library's own transports (`udp.c`, `avtp.c`, `shmem.c`) carry AVTPDUs with no authentication or encryption layer of their own. Every prior TLS-based mitigation this project's pre-TC18 TARA cited is gone (`tls.c` deprecated at v0.78.0) and no replacement is implemented here; integrators are expected to supply MACsec at the link layer, matching the same secure-by-refusal (never a silent insecure fallback) posture the retired `tls.c` stub already established.
- **TS-005**: the only threat in this TARA with a genuinely complete, working mitigation at the library level with no external backend dependency.

---
_Document owner: SoundMatt/c-RCP maintainers_
_Review date: on next threat model change or annually, whichever is sooner_
_Standard: ISO 21434:2021 Clause 9_
