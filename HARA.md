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

Each hazard's `situations` column links it to the `Operational Situations`
table above (ISO 26262-3:2018 Clause 6.4.2: hazard identification and
analysis is performed per operational situation, and `Exposure` in
particular is the probability of *that* situation, not a
situation-independent constant). `.fusa-hara.json` carries this same
`situations` reference array on every hazard entry, plus a `rationale`
string on each `risk` object spelling out, against ISO 26262-3:2018 Table
4's own S/E/C class definitions, why that hazard's recorded classification
is what it is — summarized per-hazard in the Rationale Summary below.

| ID | Hazard | Situations | Severity | Exposure | Controllability | ASIL | Safety Goals |
|----|--------|------------|----------|----------|-----------------|------|--------------|
| H-001 | Loss of safety-tagged request delivery/execution to a safety-critical endpoint | OS-001, OS-003, OS-005 | S3 | E4 | C2 | **ASIL-C** | SG-001 |
| H-002 | Request executed against the wrong endpoint due to a `(stream_id, byte_bus_id)` addressing error | OS-001 | S2 | E3 | C2 | ASIL-A | SG-002 |
| H-003 | Per-stream watchdog overflows but the endpoint is not driven toward its configured safe state | OS-001, OS-003 | S2 | E4 | C2 | ASIL-B | SG-003 |
| H-004 | Stale or duplicated request captured and re-executed | OS-001, OS-006 | S2 | E3 | C2 | ASIL-A | SG-004 |
| H-005 | A safety-tagged request executes before its endpoint has actually reached the configured safe state | OS-003, OS-007 | S3 | E3 | C2 | ASIL-B | SG-005 |
| H-006 | A register-map field write succeeds without the caller holding the required writer authorization | OS-001, OS-002, OS-006 | S2 | E3 | C2 | ASIL-A | SG-006 |
| H-007 | An unauthenticated request is accepted over the native transport in the absence of link-layer authentication | OS-006 | S3 | E2 | C2 | ASIL-A | SG-007 |
| H-008 | RC Server fails to complete its WakeUp handshake, leaving safety-relevant actuators unresponsive | OS-007 | S3 | E3 | C2 | ASIL-B | SG-008 |
| H-009 | A CRC32-corrupted request frame is executed instead of rejected | OS-001, OS-005 | S2 | E3 | C2 | ASIL-A | SG-009 |
| H-010 | Fault-injection rules persist across process/vehicle power cycles | OS-001, OS-004 | S2 | E2 | C3 | ASIL-A | SG-010 |
| H-011 | RC Server reaches `RCP_CONFIGURED` without first passing through a validated `HW_CONFIGURED` state | OS-002, OS-008 | S2 | E2 | C2 | QM | SG-011 |

---

## S/E/C Rationale Summary (Clause 6.4.3)

The full text of each hazard's classification rationale lives in
`.fusa-hara.json`'s `hazards[].risk.rationale` field (ISO 26262-3:2018
Table 4 class definitions applied against the linked operational
situation(s) above); this table gives the one-line gist of each so a
reviewer doesn't have to open the JSON to sanity-check the shape of the
argument.

| ID | Severity rationale (gist) | Exposure rationale (gist) | Controllability rationale (gist) |
|----|---------------------------|----------------------------|-----------------------------------|
| H-001 | S3: watchdog/safe-state loss during an in-flight safety maneuver (OS-003) is life-threatening, survival not assured | E4: the triggering condition (an active safety-tagged stream) is present essentially continuously across OS-001 | C2: a downstream supervisor can still normally intervene before FTTI elapses |
| H-002 | S2: wrong-endpoint execution is severe but survival probable | E3: an addressing defect, not tied to an elevated-risk situation — medium, not high, probability | C2: an operator/consistency check can typically catch it |
| H-003 | S2: safe state simply isn't driven, rather than an unsafe action executing | E4: evaluated on every overflow across the whole OS-001 envelope, same continuous exposure as H-001 | C2: same downstream-observer reasoning as H-001 |
| H-004 | S2: a replayed request repeats a previously-valid command rather than an arbitrary one | E3: needs OS-006 (an adversary) or an unusual duplicating link fault — a specific added precondition | C2: a repeated command is often idempotent-checkable downstream |
| H-005 | S3: defeats the safe-state precondition during OS-003, matching H-001's severity | E3: needs OS-007's narrower power-transition timing window, not the whole OS-001 envelope | C2: `rcp_e2e_endpoint_in_safe_state()`'s fail-closed design is the analyzed control |
| H-006 | S2: an unauthorized write is severe but doesn't by itself put a safety-tagged request in flight | E3: needs an OS-002 config fault or OS-006 adversary; the authorized-writer check is expected to hold under ordinary OS-001 operation | C2: a write to a monitored/locked field is typically observable and reversible |
| H-007 | S3: an unauthenticated command could be an arbitrary safety-tagged one, matching H-001/H-005 | E2: requires OS-006 access in the first place — gaining that access at all is itself low-probability | C2: SG-007 delegates the actual control (MACsec) to the deployment as the intended external control |
| H-008 | S3: an incomplete handshake leaves actuators unresponsive, matching H-001/H-005 | E3: bounded to OS-007's power-transition window, not the continuous OS-001 envelope | C2: `rcp_pwrmode_handshake_has_failed()` reports the failure explicitly rather than assuming completion |
| H-009 | S2: corrupted-frame execution is severe but evaluated per-frame, not across a whole maneuver window | E3: driven by OS-005 (link degradation/EMI) on top of ordinary OS-001 traffic — real but not constant | C2: stream-fault latching gives a downstream consumer an observable signal |
| H-010 | S2: a persisted rule masking/fabricating a fault is severe only if it coincides with a real safety-relevant event | E2: requires OS-004 (a software fault leaking dev/test tooling into a field session) — itself low-probability | C3: uniquely difficult to control — the persisted rule actively falsifies the signal a supervisor would use to detect it |
| H-011 | S2: an unvalidated-hardware-configuration bypass is severe in principle, though a precondition violation rather than a direct unsafe actuation | E2: needs OS-002 or occurs only within OS-008's narrow bootstrap window — an edge condition, not continuous exposure | C2: `rcp_lifecycle_transition()`'s modeled transition table is the analyzed control |

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

---

## Known Content Gaps (issue `c-RCP-22`)

A structural review of this HARA (issue `c-RCP-22`) found it fell short of
ISO 26262-3:2018 Clause 6 and of what `cfusa hara show` is built to check
in several ways. This revision closes three of those:

1. **Operational situations were prose-only** — `.fusa-hara.json` had no
   `operationalSituations[]` key at all, so no tool could verify hazard
   analysis had actually been performed per situation (Clause 6.4.2).
   **Closed** — promoted into `.fusa-hara.json`'s own schema above.
2. **Hazards weren't linked to the situations they occur in** — no
   `situations` reference array on any hazard entry. **Closed** — every
   hazard now carries one (Hazard Table above; `.fusa-hara.json`
   `hazards[].situations`).
3. **No written S/E/C classification rationale** — bare letters/numbers
   with no justification against ISO 26262-3:2018 Table 4. **Closed** —
   every hazard's `risk` object now carries a `rationale` string
   (S/E/C Rationale Summary above; full text in
   `.fusa-hara.json`).

Two gaps from that issue remain **open**, deliberately deferred rather
than forced through in this pass:

4. **The 9 protocol-bridge/adapter modules** (`grpcbridge.c`,
   `restbridge.c`, `someipbr.c`, `canbr.c`, `ddsbr.c`, `mqttbr.c`,
   `linbr.c`, `udsbr.c`, `doipbr.c`) have never been through a hazard-ID
   pass — none appear in this HARA. This is a real, separate
   hazard-identification exercise (reading each bridge's actual behavior
   and either recording a genuine new hazard or an explicit "analyzed,
   no hazard, QM" conclusion), not a mechanical field-fill, and is sized
   for its own dedicated pass.
5. **`ftti_ms` is asserted, not cross-checked** — nothing in this repo
   verifies a hazard's recorded FTTI against its implementing mechanism's
   actual measured reaction time (e.g. H-001's watchdog). Needs at least
   one sampled timing test or a documented manual-verification record.

`cfusa hara show`'s own `Hazards (0)` / partial `Safety Goals` count in
this repo is **not** a symptom of either open gap above — it's a
pre-existing limit in `cfusa`'s own `cmd_hara.c` parser (a fixed 512-byte
per-array-element buffer that silently drops any hazard/safety-goal JSON
object whose literal text exceeds that, which every entry in this file's
verbose, safety-relevant prose does). `cfusa check`'s actual gating rules
(`HARA001`–`HARA006`) scan the raw file directly and are unaffected;
`cfusa hara show`'s pretty-printer is the only thing that undercounts.
Not fixable from this repo (the parser lives in `c-FuSa`); noted here so
a future reader isn't misled by the display into thinking the hazards
themselves are missing.
