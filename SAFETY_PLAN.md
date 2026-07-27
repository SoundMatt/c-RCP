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

- All requirements in `.fusa-reqs.json` are annotated with `// fusa:req` and `// fusa:test` markers
- c-FuSa `check` enforces zero violations on every PR
- Traceability verified: every requirement must be traced to an implementation and a test
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
