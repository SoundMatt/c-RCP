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

| Spec | Module | Safety Property | Liveness Property | Fairness Required |
|------|--------|-----------------|--------------------|--------------------|
| `LifecycleStateMachine.tla` | RC Server lifecycle FSM (`include/rcp/lifecycle.h`, Phase 14) | SP1: `RcpConfigured` is only ever reached via `HwConfigured`; SP2: a field lock set while `RcpConfigured` never reverts except via full reset | LP1: given eventually-consistent HW/RCP config inputs, the server eventually reaches `RcpConfigured` | **WF** on `PromoteToHwConfigured`; **SF** on `PromoteToRcpConfigured` (WF alone is insufficient — see below) |
| `E2ESafePoint.tla` | Per-stream watchdog + safety-tagged execution gate (`include/rcp/e2e.h`, Phase 18) | SP1: a watchdog-overflow purge never discards a pending safety-tagged request; SP2: a safety-tagged request only executes once its endpoint reports safe state | LP1: a safety-tagged request that becomes pending eventually executes, given an eventually-stable per-stream safe-state signal | **WF**, per stream, on `ExecuteSafety(s)` (sufficient — SF is not required) |

The liveness properties (LP1 in both specs) were added by c-RCP-23d (issue
#602), extending verification from bounded safety-invariant checking to
fairness-conditioned liveness. See "Liveness Properties" below for what
each one actually claims, the fairness level each genuinely needs (independently
re-derived against real TLC runs, not assumed), and an important honesty
note distinguishing this from the pre-existing deadlock-freedom guarantee.

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
see each spec's own state-count summary below. As of c-RCP-23d (issue
#602), each `.cfg`'s `SPECIFICATION` line points at a `FairSpec` formula
(the original `Spec` plus the minimum fairness each spec's liveness
property needs — see "Liveness Properties" below), so this exact same
invocation — no `-config` flag, no CI change — now also checks each
spec's LP1 liveness property alongside its pre-existing safety
invariants and properties in the same run.

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

## Liveness Properties (c-RCP-23d, issue #602)

The safety properties above (and the invariants) are exhaustively
checked over a **bounded, finite** state space — they say "nothing bad
ever happens," not "something good eventually does." TLC's default
model-checking run also always includes a no-successor-state deadlock
check (every reachable state must have at least one enabled successor
under `Next`), and **that check has already passed on every CI run
since these two specs were added — it predates issue #602 and is not
this issue's contribution.** What issue #602 actually adds is a
strictly *stronger*, related claim: not just "the model never gets
stuck with zero enabled actions," but "under fair scheduling, the
system actually *makes progress* toward a specific good state" —
livelock-freedom, not merely deadlock-freedom. A spec can be
deadlock-free by TLC's default check and still livelock forever (e.g.
an adversarial scheduler could, in principle, alternate two enabled
actions forever without the system ever reaching the state that
matters) — LP1 in each spec below is what rules that out.

### LifecycleStateMachine — LP1: Eventually Reaches RcpConfigured

**Claim**: given HW/RCP configuration inputs that eventually settle and
stay consistent (`InputsEventuallyConsistent ==
<>[](hw_cfg_consistent /\ rcp_cfg_consistent)`), the server eventually
reaches `RcpConfigured` under fair scheduling of its two promote
actions.

**Fairness-minimality experiment.** The natural first guess — weak
fairness (WF) on both `PromoteToHwConfigured` and
`PromoteToRcpConfigured` — was independently built and run against real
TLC as a separate variant and **fails**: TLC finds a concrete
Promote/Demote lasso counterexample that never leaves
`{HwUnconfigured, HwConfigured}` —

```
State 1: HwUnconfigured, hw_cfg_consistent=TRUE, rcp_cfg_consistent=TRUE
State 2: PromoteToHwConfigured -> HwConfigured
Back to State 1: DemoteToHwUnconfigured
```

`DemoteToHwUnconfigured` is unconditionally enabled at `HwConfigured`
and races `PromoteToRcpConfigured` back to `HwUnconfigured` every time,
so `PromoteToRcpConfigured` is only ever *intermittently* enabled, never
*continuously* enabled — the condition WF acts on. **Strong fairness
(SF) on `PromoteToRcpConfigured` specifically** is what fixes this (SF
acts on infinitely-often-enabled, not just continuously-enabled), and a
corrected mixed-fairness variant — WF on `PromoteToHwConfigured`, SF on
`PromoteToRcpConfigured` — was independently built and confirmed to pass
against real TLC.

A separate isolated check (WF only on `PromoteToHwConfigured`, checking
only that the server leaves `HwUnconfigured`, with no fairness anywhere
else) also passed — confirming WF genuinely is the *sufficient* (not
just convenient) minimum for that specific action: nothing else in the
spec can change `state` away from `HwUnconfigured` except
`PromoteToHwConfigured` itself, so once `hw_cfg_consistent` and
`state = HwUnconfigured` hold continuously, nothing can disable it
before it fires. Blanket-applying SF to both actions would have been a
strictly weaker verification result presented as a stronger one (masking
that `PromoteToHwConfigured` never needed it), so the spec uses WF there
and SF only where TLC shows it is actually required.

**State space**: 12 distinct reachable states (same bounded space as the
safety properties — LP1 adds no new reachable states, only a temporal
claim over the existing ones). TLC confirms `Model checking completed.
No error has been found.` for the full `FairSpec` (`TypeOK`,
`NoSkipConfiguration`, `FieldLockMonotonicWhileConfigured`,
`EventuallyRcpConfigured`) in under 1 second.

### E2ESafePoint — LP1: A Pending Safety Request Eventually Executes

Issue #602's own suggested wording for this spec — "a pending
safety-tagged request is eventually either executed or purged, never
stuck pending forever" — is **false by construction** against this
spec's own SP1: `Miss(s)` (the only purge event) never touches
`safety_pending` by design (that is exactly what SP1 verifies), so
"purged" is never a live alternative for a safety-tagged request.
Independently re-running TLC against that literal wording confirms it:
the counterexample is a trivial submit-and-never-purge-or-execute
stutter, not a meaningful liveness gap. The corrected, narrower claim
this spec can actually make — and what is verified here — is:

**Claim**: for each stream `s`, given `s`'s endpoint safe-state signal
eventually settling and staying `TRUE`
(`EndpointEventuallyStable(s) == <>[](endpoint_in_safe_state[s])`), a
safety-tagged request that becomes pending on `s` eventually executes,
under weak-fair scheduling of `ExecuteSafety(s)`.

**Fairness-minimality experiment.** A per-stream **weak fairness (WF)**
variant (`WF_vars(ExecuteSafety(s))` for each stream, no SF anywhere)
was independently built and confirmed to pass against real TLC — SF is
**not** required here, unlike the Lifecycle spec's
`PromoteToRcpConfigured`. The reason WF suffices: nothing in this spec
can clear `safety_pending[s]` except `ExecuteSafety(s)` itself (`Miss`
leaves it untouched, per SP1), so once `safety_pending[s]` holds and
`endpoint_in_safe_state[s]` holds continuously, `ExecuteSafety(s)` stays
*continuously* enabled until taken — exactly the condition WF (not just
SF) acts on. There is no equivalent of the Lifecycle spec's
Demote-race here disabling the action out from under a continuously-true
guard.

Two further variants confirm both halves of the claim are load-bearing,
not decorative:
- Dropping the `EndpointEventuallyStable(s)` antecedent (same WF
  fairness, no endpoint-stability assumption) **fails**: TLC finds a
  counterexample where `ObserveSafeState` perpetually flips
  `endpoint_in_safe_state[s]` true/false in lockstep with `ExecuteSafety`
  becoming enabled, so it is never *continuously* enabled and WF never
  triggers.
- Dropping fairness entirely (same antecedent, `Spec` with no `WF`)
  **fails**: TLC finds a stuttering counterexample where a pending
  safety request with a permanently-safe endpoint simply never executes
  because nothing forces the model to ever take an enabled step.

**State space (default 2-stream config)**: 256 distinct states,
sub-second. **Widened 4-stream configuration** (`Streams = {s1,s2,s3,s4}`,
`SafestateEnabled = {s1,s3}`, `WatchdogEnabled = {s1,s2,s3,s4}`, run
against the actual `FairSpec` including `EventuallySafetyExecutes`, not
just the two safety properties): **65,536 distinct states**
(5,308,432 states generated), all properties — `TypeOK`,
`SafetyRequestsSurvivePurge`, `NoUnsafeSafetyExecution`,
`EventuallySafetyExecutes` — still hold, in roughly 6–7 seconds of
TLC model-checking time (about 7.5s wall-clock including JVM startup,
`-workers 4`, run locally; timing is hardware-dependent and given for
scale, not as a regression gate). 65,536 is the mathematically expected
figure for this configuration — 4 boolean-valued functions
(`overflowed`, `endpoint_in_safe_state`, `safety_pending`,
`normal_pending`) over 4 streams is `2^(4*4) = 2^16 = 65536`
possible variable assignments, and this widened config's `Init` and
`Next` make every one of them reachable. This widened-configuration
check is not itself wired into CI (the checked-in `.cfg` stays at the
2-stream default for CI runtime); it is a one-off scaling sanity check,
independently re-run for this issue, recorded here for anyone verifying
this spec's state-space growth by hand.

**Traceability**: REQ-E2E-011 through REQ-E2E-015, REQ-E2E-026.

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

## Liveness Traceability

| TLA+ Concept | Meaning |
|--------------|---------|
| `FairSpec` (both specs) | `Spec` plus the minimum fairness each spec's LP1 needs — see "Liveness Properties" above for how each fairness level was derived, not assumed |
| `LifecycleStateMachine.InputsEventuallyConsistent` | Environment assumption: the HW/RCP plausibility checks' verdicts eventually settle and stay `TRUE` |
| `E2ESafePoint.EndpointEventuallyStable(s)` | Environment assumption: stream `s`'s polled safe-state measurement eventually settles and stays `TRUE` |
