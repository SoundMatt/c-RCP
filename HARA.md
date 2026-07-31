# Hazard Analysis and Risk Assessment (HARA)

**Standard:** ISO 26262:2018 Part 3
**System:** c-RCP — OPEN Alliance TC18 Remote Control Protocol implementation
**Target ASIL:** ASIL-B baseline, with one ASIL-C hazard identified below (see ASIL Determination Note)
**Source:** `.fusa-hara.json` (machine-readable authoritative source)

---

## Re-derivation note (Phase 22, milestone 85)

This HARA fully replaces the pre-TC18 hazard set (Zone/Command model,
watchdog Healthy/Degraded/Faulted health state machine, CRC-16
sequence-counter anti-replay guard). It is re-derived directly against
the safety mechanisms Phases 13–21 actually implemented — the
register-map access-control model (`regmap.h`), the RC Server lifecycle
state machine (`lifecycle.h`), the CRC32 safe-point / safety-tagged
request execution gate and per-stream watchdog (`e2e.h`), and the
Normal/StandBy/Sleep/Unpowered power-mode model with its WakeUp
handshake (`power.h`) — not carried over from the retired catalog. Two
of the eleven hazards below (H-004, H-007) record genuinely open,
currently-unmitigated gaps rather than implemented controls; they are
recorded honestly as open items, matching this project's practice
throughout the roadmap of not asserting an unverified mitigation (see
also `tara.md`'s TS-002/TS-001/TS-003 notes on residual risk).

---

## Operational Situations

| ID | Description |
|----|-------------|
| OS-001 | Normal operation — RC Server `RCP_CONFIGURED`, all admitted request streams active |
| OS-002 | HW/RCP configuration fault — HW pin map or endpoint/stream manifest inconsistent; server cannot reach or remain in `RCP_CONFIGURED` |
| OS-003 | Safety-critical maneuver — a safety-tagged (MSB-set) request in flight against a safety-relevant actuator endpoint |
| OS-004 | HPC/application software fault — runaway process, crash, or malformed manifest driving the RC Server |
| OS-005 | Elevated network latency/jitter — congestion, EMI, or degraded Ethernet/CAN(FD/XL) link affecting AVTPDU delivery timing |
| OS-006 | Adversarial access — attacker present on the native Ethernet/AVB segment or the IEEE1722-over-UDP/IP path |
| OS-007 | Power transition — cold-start/hot-start WakeUp handshake in progress after a StandBy/Sleep period |
| OS-008 | Discovery/bootstrap — server in `HW_UNCONFIGURED`, admitting only the discovery request at the reserved discovery `byte_bus_id` |

---

## Hazard Table

| ID | Hazard | Severity | Exposure | Controllability | ASIL | Safety Goals |
|----|--------|----------|----------|-----------------|------|--------------|
| H-001 | Loss of safety-tagged request delivery/execution to a safety-critical endpoint | S3 | E4 | C2 | **ASIL-C** | SG-001 |
| H-002 | Request executed against the wrong endpoint due to a `(stream_id, byte_bus_id)` addressing error | S2 | E3 | C2 | ASIL-A | SG-002 |
| H-003 | Per-stream watchdog overflows but the endpoint is not driven toward its configured safe state | S2 | E4 | C2 | ASIL-B | SG-003 |
| H-004 | Stale or duplicated request captured and re-executed | S2 | E3 | C2 | ASIL-A | SG-004 |
| H-005 | A safety-tagged request executes before its endpoint has actually reached the configured safe state | S3 | E3 | C2 | ASIL-B | SG-005 |
| H-006 | A register-map field write succeeds without the caller holding the required writer authorization | S2 | E3 | C2 | ASIL-A | SG-006 |
| H-007 | An unauthenticated request is accepted over the native transport in the absence of link-layer authentication | S3 | E2 | C2 | ASIL-A | SG-007 |
| H-008 | RC Server fails to complete its WakeUp handshake, leaving safety-relevant actuators unresponsive | S3 | E3 | C2 | ASIL-B | SG-008 |
| H-009 | A CRC32-corrupted request frame is executed instead of rejected | S2 | E3 | C2 | ASIL-A | SG-009 |
| H-010 | Fault-injection rules persist across process/vehicle power cycles | S2 | E2 | C3 | ASIL-A | SG-010 |
| H-011 | RC Server reaches `RCP_CONFIGURED` without first passing through a validated `HW_CONFIGURED` state | S2 | E2 | C2 | QM | SG-011 |

---

## ASIL Determination Note

ASIL letters are computed via `cfusa hara asil` (ISO 26262-3:2018
Table 4). **Corrected 2026-07-30** (c-RCP CI's `cfusa` pin bump to
v0.5.50): the shared `cfusa_compute_asil()` table in `c-FuSa` itself
over-assigned ASIL in 19 of its 36 S×E×C cells prior to v0.5.50 (all S2
rows except E1, and every S3 row) — see
<https://github.com/SoundMatt/c-FuSa/releases/tag/v0.5.50>. Every one of
this HARA's 11 recorded ASIL letters had been computed against that
wrong table and was one-to-two bands too high; all 11 are corrected in
this revision, re-derived directly against `cfusa hara asil` v0.5.50
(independently cross-checked against ISO 26262-3:2018 Table 4's real
S×E×C lookup by hand, not merely trusted from the tool). **Milestone
97's "Re-verify HARA ASIL audit finding" conclusion (v0.97.0) is
superseded**: it re-ran the same recorded S/E/C triples through
`cfusa hara asil` and found them self-consistent, correctly rejecting a
contemporaneous audit's simplified linear-sum heuristic as not matching
Table 4's real shape — but the *tool itself* was wrong at the time, not
just the audit's shortcut, so "self-consistent with a buggy tool" wasn't
the same as "correct." (The audit's own arithmetic, as it happens, is
exactly ISO 26262-3:2018 Table 4's real S+E+C-additive shape, `sum <= 6`
→ QM, `7` → A, `8` → B, `9` → C, `10` → D — its numeric conclusions were
right, even though its stated reasoning that this was merely a
"heuristic" undersold it.)

Only **one** hazard now resolves above the ASIL-B baseline: H-001
(safety-tagged request delivery/execution, S3/E4) computes to ASIL-C.
Three more — H-003 (watchdog-driven safe-state entry, S2/E4), H-005
(safety-tagged request executes before safe-state reached, S3/E3), and
H-008 (WakeUp handshake failure, S3/E3) — land exactly at the ASIL-B
baseline (previously mis-recorded as ASIL-C/ASIL-D), so no ASIL
decomposition argument (ISO 26262-9 §5) is needed for them after all.
H-011 (lifecycle configuration-validation bypass) drops to QM — it is
no longer a safety-relevant hazard under Table 4's real S2/E2/C2 cell.
H-001 remains the sole hazard exceeding the ASIL-B baseline; its
mitigation (`e2e.c`'s watchdog/safe-state gate) is implemented and
tested at its own correctness level, but a full ASIL decomposition
argument or ASIL-C-specific process rigor has not been separately
pursued.

H-007 (unauthenticated request over the native transport, S3/E2) now
computes to ASIL-A, not ASIL-C — but remains a genuinely open,
**unmitigated** item within this library regardless of its ASIL letter:
it has **no ASIL-relevant control implemented at all** in this
repository, since MACsec is out of this library's scope entirely.

**H-004 and H-007 are open, not process gaps on an implemented
mechanism.** H-004 (replay) and H-007 (link-layer authentication) have
**no implemented control in this library at all** to fall short of.
Both are honestly recorded as open in `.fusa-hara.json`'s `safe_state`
field and in `tara.md`'s residual-risk notes, not asserted as closed.

---

## Safety Goals

| ID | Safety Goal | ASIL | Addressed By |
|----|-------------|------|--------------|
| SG-001 | Safety-tagged requests to a safety-critical endpoint shall execute, or the endpoint shall be driven to a defined safe state, within the configured per-stream watchdog timeout. | ASIL-C | `rcp_e2e_wd_evaluate()`, `rcp_watchdog_keeper_t` |
| SG-002 | Requests shall only be executed against the endpoint identified by their `(stream_id, byte_bus_id)` address; a request naming an unregistered address shall be rejected. | ASIL-A | `avtp.h` addressing, `rcp_mock_server_dispatch()` unregistered-`byte_bus_id` handling |
| SG-003 | A per-stream watchdog overflow with `rx_wd_safestate_enable` set shall drive the endpoint toward its configured safe state and shall never discard a pending safety-tagged request in doing so. | ASIL-B | `rcp_e2e_wd_evaluate()`, `rcp_e2e_watchdog_purge_should_keep()`/`_classify()`; formally verified (`tla/E2ESafePoint.tla` `SafetyRequestsSurvivePurge`) |
| SG-004 | Replayed or duplicated requests should be detected and rejected. | ASIL-A | **Open — no implemented mitigation.** See ASIL Determination Note and `tara.md` TS-002. |
| SG-005 | A safety-tagged request shall execute only once its endpoint reports it has reached the configured safe state. | ASIL-B | `rcp_e2e_request_may_execute()`, `rcp_e2e_endpoint_in_safe_state()`; formally verified (`tla/E2ESafePoint.tla` `NoUnsafeSafetyExecution`) |
| SG-006 | Register-map field writes shall be permitted only for the authorized writer context, and a `FUNCTIONAL_W_STAR`-class field, once locked while `RCP_CONFIGURED`, shall never accept a write until a full reset. | ASIL-A | `rcp_regmap_writer_ctx()`; formally verified (`tla/LifecycleStateMachine.tla` `FieldLockMonotonicWhileConfigured`) |
| SG-007 | Link-layer authentication (MACsec) shall be enforced by the deployment on any transport carrying safety-relevant requests. | ASIL-A | **Deployment-level control; not implemented within this library.** See ASIL Determination Note and `tara.md` TS-001/TS-003. |
| SG-008 | The RC Server shall only resume normal operation after a successfully completed WakeUp handshake step sequence. | ASIL-B | `rcp_pwrmode_handshake_is_complete()`, `rcp_pwrmode_handshake_has_failed()`, `rcp_pwrmode_wake_from_sleep()` |
| SG-009 | A CRC32-mismatched request frame shall never have its request executed. | ASIL-A | `rcp_e2e_unwrap()`, `rcp_e2e_crc_error_action()`, `rcp_e2e_stream_fault_t` |
| SG-010 | Fault-injection rules shall not persist beyond the lifetime of the injecting process. | ASIL-A | `rcp_faultinject_*` in-process-only state |
| SG-011 | The RC Server's lifecycle state shall never advance to `RCP_CONFIGURED` without first passing through a validated `HW_CONFIGURED` state. | QM | `rcp_lifecycle_transition()`; formally verified (`tla/LifecycleStateMachine.tla` `NoSkipConfiguration`) |

---

## Residual Risks

| Risk | Likelihood | Mitigation | Status |
|------|-----------|------------|--------|
| No replay/staleness detection for captured-and-resent requests (H-004) | Medium | None implemented in this library; the retired CRC-16 sequence-counter/replay-window mechanism was not carried forward, and the TC18 CRC32 safe-point mechanism that replaced it does not reimplement replay detection (`include/rcp/e2e.h`'s own file header records this gap explicitly) | **Open — tracked, not mitigated** |
| No link-layer authentication for the native transport in this library's default posture (H-007) | Medium | MACsec (802.1AE) is a link-layer, product-specific/opaque control the spec delegates outside RCP itself; an integrator must supply it | **Open — deployment responsibility, not closed by this library** |
| `rcp_e2e_endpoint_in_safe_state()` misconfiguration (invalid `safestate_sequencer` index, unrecognized `rx_safety_measure`) | Low | Fails closed (returns false) by explicit design choice, not a spec-mandated value | Accepted |
| Discovery/bootstrap (`OS-008`) accepts any claimant at the reserved discovery `byte_bus_id` while `HW_UNCONFIGURED` | Low | No identity check exists at this stage in the spec's own bootstrap sequence; matches the same residual posture as H-007 above | Accepted, tracked alongside H-007 |
| One hazard (H-001) computes to ASIL-C against an ASIL-B-scoped implementation rigor | Open | See ASIL Determination Note above | **Open — not yet closed** |
