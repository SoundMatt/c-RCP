# Formal Verification — c-RCP (Milestone 85)

## Overview

TLA+ specifications are located in [`tla/`](tla/). They cover the two
safety-critical subsystems the Phase 22 re-certification pass (see
`ROADMAP.md` milestone 85) identified as the actual TC18 safety
mechanisms this codebase now ships, replacing the three specs that
modeled the retired Zone/Command-era watchdog and CRC-16 anti-replay
guard (`WatchdogProtocol.tla`, `HealthStateMachine.tla`,
`AntiReplayGuard.tla` — all three deleted by this milestone, not
adapted, since their subject — a per-zone Healthy/Degraded/Faulted
health state machine and a sequence-number replay window — has no TC18
counterpart; `rcp_zone_t`/`rcp_controller_t` were retired at Phase 13-14
and `include/rcp/e2e.h`'s own file header records that this codebase's
CRC32 mechanism does not reimplement a sequence-counter/replay-window
concept at all):

| Spec | Module | Safety Property |
|------|--------|-----------------|
| `LifecycleStateMachine.tla` | RC Server lifecycle FSM (`include/rcp/lifecycle.h`, Phase 14) | SP1: `RcpConfigured` is only ever reached via `HwConfigured`; SP2: a field lock set while `RcpConfigured` never reverts except via full reset |
| `E2ESafePoint.tla` | Per-stream watchdog + safety-tagged execution gate (`include/rcp/e2e.h`, Phase 18) | SP1: a watchdog-overflow purge never discards a pending safety-tagged request; SP2: a safety-tagged request only executes once its endpoint reports safe state |

Unlike the specs they replace (ported verbatim from cpp-RCP's own `tla/`
directory, per the pre-Phase-13 "feature and API mirror" convention this
project no longer follows — see `ROADMAP.md`'s Protocol Replacement
Notice), these two are original models of this codebase's own TC18
lifecycle and E2E implementation. No spec prose, on-wire byte value, or
numeric constant from the OPEN Alliance TC18 Remote Control Protocol
Specification v0.5.1_RC is reproduced in either `.tla` file; both use
this project's own symbolic state names (already established as C
identifiers in `include/rcp/lifecycle.h` / `include/rcp/e2e.h`) and
model only the control-flow shape of the corresponding pure C functions.

## Verification Method

Specs are verified with the TLC model checker (`tla2tools.jar`, TLC2
2.19+). Each spec has a matching `.cfg` file in `tla/` supplying its
`CONSTANTS`; TLC picks up `<Spec>.cfg` automatically for `<Spec>.tla`.

```bash
curl -sSL -o tla2tools.jar https://github.com/tlaplus/tlaplus/releases/latest/download/tla2tools.jar
java -jar tla2tools.jar -workers 4 tla/LifecycleStateMachine.tla
java -jar tla2tools.jar -workers 4 tla/E2ESafePoint.tla
```

Expected output: `Model checking completed. No error has been found.`
This is wired into CI (`.github/workflows/ci.yml`'s `formal-verification`
job) — every push/PR against `main` re-runs both model checks and fails
the job on any violation. Both specs were run locally through TLC before
this milestone's PR was opened (not merely written and assumed correct);
see each spec's own state-count summary below.

## Safety Properties Verified

### LifecycleStateMachine — SP1: No Skip-Configuration Transition

The RC Server's lifecycle state can never advance from `HwUnconfigured`
directly to `RcpConfigured`; every upward transition passes through
`HwConfigured`, gated in turn by its own HW/RCP configuration
plausibility check. Confirmed by TLC over the full 3-state, 4-boolean
reachable state space (12 distinct states, exhaustively explored).

**Traceability**: REQ-LIFECYCLE-008, REQ-LIFECYCLE-009, REQ-LIFECYCLE-012.

### LifecycleStateMachine — SP2: Field Lock Monotonicity

Once a `FUNCTIONAL_W_STAR`-class register field is locked while the
server is `RcpConfigured`, it never becomes unlocked again for as long as
the server remains `RcpConfigured` — only the full-reset transition back
to `HwUnconfigured` clears the lock.

**Traceability**: REQ-LIFECYCLE-018, REQ-LIFECYCLE-020.

### E2ESafePoint — SP1: Safety Requests Survive the Watchdog Purge

Whenever a request stream's watchdog overflows with
`rx_wd_safestate_enable` set (the event that purges every non-safety-
tagged pending request from that stream's queue), a safety-tagged
request that was pending beforehand is never discarded by that purge.

**Traceability**: REQ-E2E-014, REQ-E2E-015, REQ-E2E-026.

### E2ESafePoint — SP2: No Execution Before Safe State

A safety-tagged (MSB-set) request only ever transitions from pending to
executed while its endpoint's polled safe-state measurement reports
`TRUE`. A non-safety-tagged request is unaffected by this rule (modeled
by `ExecuteNormal`'s guard omitting `endpoint_in_safe_state` entirely).

**Traceability**: REQ-E2E-011, REQ-E2E-012, REQ-E2E-013.

## Assumptions and Abstractions

- `LifecycleStateMachine.tla` models a single RC Server with exactly one
  lifecycle state, matching this codebase's own architecture (an RC
  Server is a single node, unlike the retired per-zone watchdog model —
  see `ROADMAP.md`'s Satellite Package Disposition table's
  `redundancy.h`/`redundancy.c` entry). The HW/RCP configuration
  plausibility checks (`rcp_lifecycle_hw_cfg_consistent()` /
  `rcp_lifecycle_rcp_cfg_consistent()`-equivalent) are modeled as
  free-running boolean environment inputs (`ReviseHwConsistency` /
  `ReviseRcpConsistency`) rather than deriving their value from a
  concrete register-map/manifest content model — the real functions'
  *result* is what gates the state machine, and that result is exactly
  what this abstraction leaves nondeterministic.
- `LifecycleStateMachine.tla`'s assumption that a full reset clears a
  previously-set field lock (`FullReset`'s `field_lock' = Unlocked`) is
  this model's own engineering judgment, not a literal restatement of a
  single numbered requirement — `include/rcp/lifecycle.h` describes the
  lock and the full-reset transition separately, and this model connects
  them the way the C implementation's own field-plausibility snapshot
  reasoning does. Flagged here so a reader can independently judge
  whether the assumption matches intent, rather than treating it as an
  established fact.
- `E2ESafePoint.tla` models the watchdog's elapsed-time threshold as a
  single atomic `Miss` transition rather than an incrementing simulated
  clock (the style the retired `WatchdogProtocol.tla` used) — the
  properties of interest here concern what happens *at* the overflow
  event and afterward, not the intermediate elapsed-time values, so the
  coarser step is a faithful abstraction for this model's own goals
  without the state-space cost of a bounded clock.
- `E2ESafePoint.tla` does not model `rx_wd_info_enable`/`notify` (the
  independent informational-event output `rcp_e2e_wd_evaluate()` also
  produces) — it has no safety property of its own to verify beyond
  "an overflow occurred," which `overflowed` already captures.
- CRC32 well-formedness (`rcp_e2e_wrap()`/`rcp_e2e_unwrap()`) is a pure,
  stateless per-frame computation with no state-transition behavior
  worth model checking; it is covered by `tests/test_e2e.c`'s direct
  unit tests instead (REQ-E2E-002 through REQ-E2E-009).

## Mapping to C Implementation

| TLA+ Variable/Action | C Location |
|-----------------------|------------|
| `state` | `rcp_lifecycle_state_t` (`include/rcp/lifecycle.h`) |
| `hw_cfg_consistent` / `rcp_cfg_consistent` | the HW_CFG_INCONSISTENT / RCP_CFG_INCONSISTENT plausibility checks' return value (`src/lifecycle.c`) |
| `field_lock` | the `FUNCTIONAL_W_STAR` write-permission outcome of `rcp_regmap_writer_ctx()` (`src/regmap.c`) once `RcpConfigured` |
| `PromoteToHwConfigured` / `PromoteToRcpConfigured` | `rcp_lifecycle_transition()` (`src/lifecycle.c`) |
| `overflowed` / `enter_safe_state` | `rcp_e2e_wd_result_t` (`include/rcp/e2e.h`), produced by `rcp_e2e_wd_evaluate()` |
| `safety_pending` / `normal_pending` | a request stream's queued-but-not-yet-executed requests, classified via `rcp_e2e_watchdog_purge_should_keep()`/`_classify()` (`src/e2e.c`) |
| `endpoint_in_safe_state` | `rcp_e2e_endpoint_in_safe_state()`'s return value (`src/e2e.c`) |
| `ExecuteSafety` / `ExecuteNormal` | `rcp_e2e_request_may_execute()`'s admission decision (`src/e2e.c`) |
