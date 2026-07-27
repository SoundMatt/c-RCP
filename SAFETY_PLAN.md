# Safety Plan — c-RCP

## Scope

c-RCP is the pure-C99 implementation of the Remote Control Protocol for automotive zonal architecture targeting ISO 26262 ASIL-B / IEC 61508 SIL-2.

## Safety standard

| Standard | Target level |
|---|---|
| ISO 26262 | ASIL-B |
| IEC 61508 | SIL-2 |
| IEC 62443 | SL-2 |

## Safety goals

| ID | Description | ASIL |
|---|---|---|
| SG-002 | Misrouted commands rejected | ASIL-B |

Additional safety goals (SG-001, SG-003, SG-004, SG-007, ...) are introduced as
the mechanisms that satisfy them land — see `ROADMAP.md`. The full set mirrors
cpp-RCP's HARA and is finalized at the v0.4.0 HARA-expansion milestone.

## Safety mechanisms

| Mechanism | Requirement | Description |
|---|---|---|
| Zone mismatch detection | REQ-CTRL-025, REQ-ERR-011 | `rcp_controller_send()` rejects commands addressed to a different zone |
| Payload copy-on-send | REQ-CTRL-026 | Payload is deep-copied before handler invocation |
| Payload copy-on-publish | REQ-CTRL-027 | Published payload is deep-copied before delivery to subscribers |
| Context / deadline propagation | REQ-CTRL-004 | Expired context terminates send without invoking the handler |

## Verification approach

- Implemented requirements in `.fusa-reqs.json` are annotated with
  `//cfusa:req` and `//cfusa:test` markers in source and tests
- c-FuSa `check`/`lint`/`analyze`/`cyber` run on every PR (`check`/`lint` are
  currently non-blocking pending [c-FuSa#59](https://github.com/SoundMatt/c-FuSa/issues/59))
- Function-annotation density (every `.c` file traces to at least one
  requirement) is a hard 100% CI gate from v0.1.0
- Requirement traceability is **not** yet a hard 100% gate: as of v0.2.0,
  `.fusa-reqs.json` forward-declares the full 198-requirement SEOOC catalog
  for modules not yet implemented (REQ-UDP, REQ-E2E, REQ-WDG, ... — see
  `ROADMAP.md` milestone 2). The gate returns once every module ships.
- Tests run on Ubuntu (gcc, clang), macOS (clang), and Windows (MSVC) in CI

## Artifact locations

| Artifact | Path |
|---|---|
| Requirements | `.fusa-reqs.json` |
| HARA | `.fusa-hara.json` |
| IEC 62443 config | `.fusa-iec62443.json` |
| Check report | `check-report.json` (CI generated) |
| Incident response | `INCIDENT-RESPONSE.md` |
| Security policy | `SECURITY.md` |
