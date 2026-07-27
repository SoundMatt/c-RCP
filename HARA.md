# Hazard Analysis and Risk Assessment (HARA)

**Standard:** ISO 26262:2018 Part 3
**System:** c-RCP — Remote Control Protocol for automotive zonal architecture
**Target ASIL:** ASIL-B baseline, with ASIL-C/D hazards identified below (see ASIL Determination Note)
**Source:** `.fusa-hara.json` (machine-readable authoritative source)

---

## Operational Situations

| ID | Description |
|----|-------------|
| OS-001 | Normal vehicle operation — all zone controllers reachable |
| OS-002 | Partial network fault — one or more zone controllers unreachable |
| OS-003 | Safety-critical manoeuvre — emergency braking or collision avoidance active |
| OS-004 | HPC software fault — runaway process, crash, or OOM condition |
| OS-005 | Elevated network latency — congestion, EMI, or hardware degradation |
| OS-006 | Adversarial access — attacker present on the zone Ethernet bus |

---

## Hazard Table

| ID | Hazard | Severity | Exposure | Controllability | ASIL | Safety Goals |
|----|--------|----------|----------|-----------------|------|--------------|
| H-001 | Loss of command delivery to safety-critical zone (e.g. braking actuator) | S3 | E4 | C2 | **ASIL-D** | SG-001 |
| H-002 | Spurious command sent to wrong zone controller | S2 | E3 | C2 | ASIL-B | SG-002 |
| H-003 | Zone controller watchdog not kicked, leading to unintended reset | S2 | E4 | C2 | **ASIL-C** | SG-003 |
| H-004 | Replay of stale commands from a previous drive cycle | S2 | E3 | C2 | ASIL-B | SG-004 |
| H-005 | Zone controller falsely reported as alive when unresponsive | S2 | E3 | C2 | ASIL-B | SG-007 |
| H-006 | Priority inversion — low-priority burst starves safety-critical watchdog kick | S2 | E4 | C2 | **ASIL-C** | SG-001, SG-005 |
| H-007 | Rate limiter blocks watchdog kick during high command burst | S2 | E3 | C2 | ASIL-B | SG-003, SG-005 |
| H-008 | Unauthorized command injection via unsecured transport | S3 | E2 | C2 | **ASIL-C** | SG-006 |
| H-009 | Power state management failure — zone not properly woken from sleep | S3 | E3 | C2 | **ASIL-D** | SG-001, SG-008 |
| H-010 | Fault injection state persists across vehicle power cycles | S2 | E2 | C3 | ASIL-B | SG-009 |

---

## ASIL Determination Note

The S/E/C classifications above are carried over unchanged from cpp-RCP's own
HARA (same hazards, same operational-situation reasoning). **The ASIL
letters are not** — they are recomputed here via `cfusa hara asil`, which
implements ISO 26262-3:2018 Table 4 (with the C0 extension, in parity with
go-FuSa's table). Six of the ten hazards resolve to a higher ASIL under this
table than cpp-RCP's own HARA.md records for the identical S/E/C inputs:

| Hazard | S/E/C | cpp-RCP's ASIL | c-FuSa `hara asil` |
|---|---|---|---|
| H-001 | S3/E4/C2 | ASIL-B | **ASIL-D** |
| H-003 | S2/E4/C2 | ASIL-B | **ASIL-C** |
| H-006 | S2/E4/C2 | ASIL-B | **ASIL-C** |
| H-008 | S3/E2/C2 | ASIL-B | **ASIL-C** |
| H-009 | S3/E3/C2 | ASIL-B | **ASIL-D** |
| H-010 | S2/E2/C3 | ASIL-A | ASIL-B |

This project adopts c-FuSa's computed values as authoritative: it is the
project's designated compliance tool and directly implements the cited
standard table (rather than a value hand-recorded in a markdown file).
Whether the discrepancy is a defect in cpp-RCP's own HARA.md, a difference
between cpp-FuSa's and c-FuSa's table implementations, or an intentional
(but undocumented) decomposition already assumed by cpp-RCP is not yet
determined — it hasn't been investigated on the cpp-RCP/cpp-FuSa side.

**Consequence:** the mechanisms landing in milestones 7, 11, 13, 15, and 19
(TLS, watchdog, power state, priority queue, authorization) are currently
scoped and implemented to an ASIL-B rigor level, matching cpp-RCP's
mirrored design. Four hazards (H-001, H-003, H-006, H-008, H-009) now show
a computed target of ASIL-C or ASIL-D. Closing that gap — via a real ASIL
decomposition argument (independent ASIL-B/A elements combining to satisfy
a higher target, per ISO 26262-9) or by revising the S/E/C classification
with updated engineering rationale — is deferred to milestone 41 (Formal
Verification) and milestone 43 (Certification, which already scopes an
"ASIL-D gap analysis"). Until then, treat the ASIL-D/C figures above as the
honest current state, not yet a closed item.

---

## Safety Goals

| ID | Safety Goal | ASIL | Addressed By |
|----|-------------|------|--------------|
| SG-001 | Commands to safety-critical zones shall be delivered within the watchdog period or a fault shall be signalled. | ASIL-D | `rcp_watchdog_keeper_t`, `rcp_deadline_monitor_t` |
| SG-002 | Commands shall only be processed by the zone they are addressed to; misrouted commands shall be rejected. | ASIL-B | `rcp_controller_send()` `RCP_ERR_ZONE_MISMATCH` check |
| SG-003 | The watchdog kick command shall always be deliverable at the configured priority. | ASIL-C | `rcp_ratelimit_controller_t` Critical exemption, `rcp_prioqueue_controller_t` Critical bypass |
| SG-004 | Replayed or duplicated commands from prior sessions shall be detected and rejected. | ASIL-B | `rcp_e2e_replay_guard_t` bitmap sliding window |
| SG-005 | Critical-priority commands shall never be delayed by Normal- or High-priority commands queued earlier. | ASIL-C | `rcp_prioqueue_controller_t` priority ordering |
| SG-006 | Transport authentication (mTLS or equivalent) shall be enforced on all external zone controller connections. | ASIL-C | TLS transport, mTLS config |
| SG-007 | A zone controller that stops publishing Status shall be detected as dead within the configured deadline. | ASIL-B | `rcp_deadline_monitor_t` |
| SG-008 | A zone controller shall only be declared operational after a successful Wake command response. | ASIL-D | `rcp_powerstate_manager_t` Active transition gate |
| SG-009 | Fault injection rules shall not persist beyond the lifetime of the injecting process. | ASIL-B | `rcp_faultinject_controller_t` in-process state only |
| SG-010 | Zone controller health state transitions shall be deterministically derivable from observable `send()` outcomes alone. | ASIL-B | `rcp_watchdog_keeper_t` deterministic state machine |

---

## Residual Risks

| Risk | Likelihood | Mitigation | Status |
|------|-----------|------------|--------|
| ReplayGuard window exhausted by rapid legitimate traffic | Low | 32-entry window handles 32 in-flight commands per zone; watchdog period is at least 10 ms | Accepted |
| TLS stub returns "not supported" on non-Linux CI | Low | CI explicitly tests the shmem/mock transport; TLS tested on Linux only | Accepted |
| mDNS static discovery not sufficient for dynamic topology | Medium | Full mDNS backend deferred to milestone 6; static config covers initial SiL testing | Accepted |
| Six hazards computed at ASIL-C/D vs. an ASIL-B-scoped implementation | **Open** | See ASIL Determination Note above; tracked for milestone 41/43 | **Open — not yet closed** |
