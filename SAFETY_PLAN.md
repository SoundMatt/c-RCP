# Safety Plan — c-RCP

## Scope

c-RCP is the pure-C99 implementation of the OPEN Alliance TC18 Remote
Control Protocol, baselined at ISO 26262 ASIL-B / IEC 61508 SIL-2.

## Safety standard

| Standard | Target level |
|---|---|
| ISO 26262 | ASIL-B baseline; **HARA.md identifies one hazard computed at ASIL-C** (H-001) — open, tracked per `HARA.md`'s ASIL Determination Note, not yet closed |
| IEC 61508 | SIL-2 |
| IEC 62443 | SL-2 |

## Safety goals

See `HARA.md` for the full SG-001..SG-011 table with ASIL ratings
computed via `cfusa hara asil` (ISO 26262-3:2018 Table 4). Two safety
goals (SG-004, SG-007) are recorded as open — no implemented mitigation
exists in this library for replay/staleness detection or link-layer
authentication; see `HARA.md`'s Residual Risks and `tara.md`.

## SEOOC status

c-RCP is developed and safety-argued as a Safety Element out of
Context (ISO 26262-10:2018 Clause 9) — a library integrated later into
a vehicle item whose own HARA and ASIL assignment this repository does
not have visibility into. `SEOOC_BOUNDARY.md` is the integrator-facing
Item Definition boundary statement and consolidated Assumptions of Use
(AoU) document this implies; read it before relying on this safety
case as part of a larger item's own safety argument.

## Safety mechanisms

| Mechanism | Requirement | Description |
|---|---|---|
| Per-stream watchdog / safe-state gate | REQ-E2E-011, REQ-E2E-012, REQ-E2E-024..027 | `rcp_e2e_wd_evaluate()` overflow verdict gates a safety-tagged request's execution on the endpoint's polled safe state; formally verified (`tla/E2ESafePoint.tla`) |
| Watchdog-purge safety survival | REQ-E2E-014, REQ-E2E-015 | A watchdog-overflow purge with `rx_wd_safestate_enable` set never discards a pending safety-tagged request |
| CRC32 frame integrity | REQ-E2E-002..009 | `rcp_e2e_wrap()`/`_unwrap()` append/validate a CRC32 trailer over the AVTPDU/ACF frame |
| CRC-error handling | REQ-E2E-020, REQ-E2E-021 | `rcp_e2e_crc_error_action()` maps `rx_enforce_e2e` to drop-request or latch-stream-fault |
| Register-map write authorization | REQ-RMAP-009..012, REQ-LIFECYCLE-018..020 | `rcp_regmap_writer_ctx()` gates field writes; formally verified (`tla/LifecycleStateMachine.tla`) |
| Lifecycle configuration gating | REQ-LIFECYCLE-008, -009, -012 | `RCP_CONFIGURED` is reachable only via `HW_CONFIGURED`; formally verified (`tla/LifecycleStateMachine.tla`) |
| WakeUp handshake completion gate | REQ-PWRMODE-006..011 | Normal operation resumes only after `rcp_pwrmode_handshake_is_complete()` |

## Verification approach

- Implemented requirements in `.fusa-reqs.json` are annotated with
  `//cfusa:req` and `//cfusa:test` markers in source and tests
- c-FuSa `check`/`lint`/`analyze`/`cyber` run on every PR as hard gates
- Function-annotation density (every `.c` file traces to at least one
  requirement) is a hard 100% CI gate from v0.1.0
- Requirement traceability (`cfusa trace --req-coverage 100`) has been a
  hard 100% gate since v0.53.0. `.fusa-reqs.json` currently carries 1095
  requirements across four `scope` values — `tc18` (947: this project's
  actual ISO 26262 safety-case basis), `tc18-gap` (136: TC18 normative
  clauses not implemented, pinned rather than omitted), `retired` (6:
  superseded/citation-corrected entries), and `internal` (6, QM: the
  allocator-hook indirection) — see `SEOOC_BOUNDARY.md` §4 for the
  freedom-from-interference argument over this partition and
  `AUDIT_PACK.md` §1 for the full breakdown. (The pre-TC18 `scope:
  "legacy-compat"` surface this line previously described was fully
  removed at v0.91.0 and no longer exists in either the code or the
  catalog.)
- Two safety mechanisms are formally verified via TLC model checking
  (`tla/`, see `FORMAL_VERIFICATION.md`), wired into CI's
  `formal-verification` job
- Tests run on Ubuntu (gcc, clang), macOS (clang), and Windows (MSVC) in CI

## Artifact locations

| Artifact | Path |
|---|---|
| Requirements | `.fusa-reqs.json` |
| HARA | `HARA.md` / `.fusa-hara.json` |
| TARA | `tara.md` / `tara.json` |
| Cybersecurity architecture | `CYBERSECURITY.md` |
| Formal verification | `FORMAL_VERIFICATION.md` / `tla/*.tla` |
| SEOOC boundary & Assumptions of Use | `SEOOC_BOUNDARY.md` |
| Tool qualification evidence | `qualify-report.json` |
| IEC 62443 config | `.fusa-iec62443.json` |
| Incident response | `INCIDENT-RESPONSE.md` |
| Security policy | `SECURITY.md` |
