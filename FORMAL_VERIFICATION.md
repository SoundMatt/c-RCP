# Formal Verification — c-RCP (Milestone 41)

## Overview

TLA+ specifications are located in [`tla/`](tla/). They cover three safety-
critical subsystems identified in the HARA (see [`HARA.md`](HARA.md)):

| Spec | Module | Safety Property |
|------|--------|----------------|
| `HealthStateMachine.tla` | Watchdog health state machine | SP1: No Healthy→Faulted direct transition |
| `AntiReplayGuard.tla`    | E2E sequence-number replay guard | SP1: No double-acceptance; SP2: Old sequences rejected |
| `WatchdogProtocol.tla`   | Watchdog heartbeat protocol | SP1: No Healthy→Faulted direct transition |

These specs are ported verbatim from cpp-RCP's `tla/` directory: they
describe the watchdog health state machine, heartbeat protocol, and
anti-replay guard at the level of state transitions and safety
properties, with no reference to C++ (or C) syntax anywhere in the
model. Because c-RCP's `watchdog.c` and `e2e.c` implement the identical
state machine and sliding-window algorithm as cpp-RCP's `watchdog.hpp`
and `e2e.hpp` (same states, same transition guards, same window size),
the same specs apply to this port without modification — only the
"Mapping to Implementation" table below changes, to point at this
project's actual C symbol names instead of cpp-RCP's C++ ones.

## Verification Method

Specs are verified with the TLC model checker (`tla2tools.jar`, TLC2
2.19+). Each spec has a matching `.cfg` file in `tla/` bounding its
CONSTANTS to finite model values (TLC cannot enumerate the specs'
originally-infinite `Nat`/free-running-clock ranges — see "Corrections"
below); TLC picks up `<Spec>.cfg` automatically for `<Spec>.tla`.

```bash
curl -sSL -o tla2tools.jar https://github.com/tlaplus/tlaplus/releases/latest/download/tla2tools.jar
java -jar tla2tools.jar -workers 4 tla/HealthStateMachine.tla
java -jar tla2tools.jar -workers 4 tla/AntiReplayGuard.tla
java -jar tla2tools.jar -workers 4 tla/WatchdogProtocol.tla
```

Expected output: `Model checking completed. No error has been found.`
This is now **wired into CI** (`.github/workflows/ci.yml`'s
`formal-verification` job) — every push/PR against `main` re-runs all
three model checks and fails the job on any violation.

## Corrections (2026-07-28, issue #57)

A deep audit found this document's claim of a clean TLC pass was false
as of v0.56.0: no `.cfg` existed for any spec, no CI job ever ran TLC,
`AntiReplayGuard.tla` did not even parse for model-checking
(`\E n \in Nat : ...` is a non-enumerable quantifier bound TLC rejects
outright), and after bounding `Nat` just to make it checkable, the
`NoDoubleAccept` property was found to be backwards — it asserted
previously-accepted elements must *leave* the accepted set, which never
happens, since `accepted` only grows. The spec had evidently never
actually been run through TLC before this document's claim was
committed. Fixed:

- **`AntiReplayGuard.tla`**: bounded the previously-unbounded `n \in Nat`
  quantifier to a new `MaxSeq` CONSTANT (`\E n \in 0..MaxSeq`). Corrected
  `NoDoubleAccept` from `\A n \in accepted : n \notin accepted'`
  (backwards) to `\A n \in accepted : n \in accepted'` — the model's
  actual guarantee is that `accepted` is monotonic (once accepted, never
  forgotten), which combined with `Check(n)`'s own precondition
  (`n \notin accepted`) is what "no double acceptance" means for this
  model. Moved it to `PROPERTIES` in the `.cfg` (it's a temporal formula
  with primed variables — TLC's `INVARIANTS` section only accepts plain
  state predicates).
- **`HealthStateMachine.tla`**: `miss_count` grew without bound (`Miss(z)`
  incremented it unconditionally even after a zone reached `Faulted`),
  making the state space genuinely infinite. Capped it at `MaxMiss` in
  the model itself — a model-checking-only bound with no behavioral
  effect, since no transition logic ever reads `miss_count` past the
  point it first reaches `MaxMiss`, and the real `rcp_watchdog_keeper_t`
  misses counter is likewise never read past that point in `src/watchdog.c`.
- **`WatchdogProtocol.tla`**: `clock` is a genuinely free-running counter
  (`Tick` always fires `clock' = clock + 1`, no natural fixed point) —
  bounded via a `ClockBound` state CONSTRAINT (`clock <= MaxClock`) in
  the `.cfg`, the standard TLA+ idiom for an otherwise-unbounded
  simulated clock. This does not change the spec, only how far TLC
  explores it.
- All three specs, once fixed, were re-run and confirmed to genuinely
  print `Model checking completed. No error has been found.` — not
  assumed from the corrected code, actually executed.

## Safety Properties Verified

### SP1 — No Direct Health Transition (HealthStateMachine, WatchdogProtocol)

A zone controller in `Healthy` state may never transition directly to
`Faulted`. It must pass through `Degraded`. This prevents a single
missed heartbeat from triggering an emergency shutdown.

**ASIL tracing**: H-002 (watchdog miss), SG-002 (watchdog recovery), REQ-WDG-003.

### SP2 — Anti-Replay Double-Acceptance (AntiReplayGuard)

A sequence number that has been accepted by the E2E guard may never be
accepted again within the replay window. Sequence numbers older than
`ReplayWindowSize` ticks are unconditionally rejected.

**ASIL tracing**: H-008 (unauthorized injection), SG-006 (mTLS + E2E), REQ-E2E-004, REQ-E2E-005, REQ-E2E-006.

## Assumptions and Abstractions

- The TLA+ models use natural numbers as simulated clocks; overflow is
  not modelled (production uses 32-bit wrap-around with correct
  comparison, per `rcp_e2e_replay_guard_t`'s `uint32_t high_water`).
- The `AntiReplayGuard` model does not model counter rollover; the C
  implementation handles rollover via unsigned arithmetic
  (`src/e2e.c`'s `age = g->high_water - seq_num` relies on `uint32_t`
  wraparound, identical to cpp-RCP's own approach).
- The `WatchdogProtocol` model assumes synchronous ticks; c-RCP's
  implementation (like cpp-RCP's) uses a background thread — here
  `rcp_thread_start()` over `platform.h`'s portable mutex/condvar shim
  rather than `std::thread`/`std::condition_variable` — polling at the
  configured interval (`rcp_watchdog_config_t.interval_ms`).
- `HealthStateMachine.tla`'s `miss_count`/`MaxMiss` model maps onto
  `rcp_watchdog_keeper_t`'s per-zone `misses` counter and the
  `degrade_after`/`fault_after` thresholds in `rcp_watchdog_config_t`
  more directly than onto a clock-based model — the two-threshold
  design (`degrade_after` then `fault_after`) is a strict refinement of
  the single-`MaxMiss` model here: an additional intermediate step
  before `Degraded`→`Faulted`, but the same `NoDirectFault` safety
  property still holds since `Healthy`→`Faulted` still requires passing
  through `Degraded`.

## Mapping to C Implementation

| TLA+ Variable | C Location |
|---------------|------------|
| `state[z]` | `zone_state_t.health` (`src/watchdog.c`), values from `rcp_health_state_t` (`include/rcp/watchdog.h`) |
| `miss_count[z]` | `zone_state_t.misses` (`src/watchdog.c`) |
| `accepted` / `high_water` | `rcp_e2e_replay_guard_t.bitmap[]` / `.high_water` (`src/e2e.c`) |
| `last_kick[z]` | tracked implicitly via `misses` reset to 0 on kick, rather than a timestamp (see abstraction note above) |
