# Audit Pack — c-RCP Certification Evidence (Milestone 85)

**Document version**: 2.0.0
**Standards**: ISO 26262 (ASIL-B), IEC 61508 (SIL-2), ISO 21434, IEC 62443 SL-2

This document fully replaces its pre-TC18 (v1.0.0) content, which
described the retired Zone/Command protocol's certification evidence and
mirrored cpp-RCP's own `AUDIT_PACK.md` structure/derogation rationale.
As of `ROADMAP.md` milestone 85 (Phase 22 re-certification pass), this
project no longer mirrors cpp-RCP port-for-port (Protocol Replacement
Notice, Phase 13) — the sections below describe c-RCP's own shipped
TC18 protocol and its own measured evidence.

---

## 1. Document Index

| Document | Location | Status |
|----------|----------|--------|
| HARA (Hazard Analysis & Risk Assessment) | `HARA.md` / `.fusa-hara.json` | Complete — re-derived, Phase 22 |
| TARA (Threat Analysis & Risk Assessment) | `tara.md` / `tara.json` | Complete (hand-authored) — re-derived, Phase 22 |
| Cybersecurity Architecture | `CYBERSECURITY.md` | Complete — re-derived, Phase 22 |
| Formal Verification | `FORMAL_VERIFICATION.md` + `tla/*.tla` | Complete — re-derived, Phase 22 |
| Portability Audit | `PORTABILITY.md` | Complete (unaffected by the protocol replacement — KEEP AS-IS per `ROADMAP.md`'s Satellite Package Disposition table) |
| Safety Requirements | `.fusa-reqs.json` | 854 requirements; 779 `scope: "tc18"` (this project's ISO 26262 safety-case basis), 75 `scope: "legacy-compat"` (retired pre-TC18 surface, `level`/`asil` demoted to `QM`) — see the file's own `catalogNote` |
| Safety Case | `safety-case.md` (auto-generated, `cfusa safety-case --gsn`) | CI gate |
| Release Badge | `fusa-badge.svg` (auto-generated, `cfusa badge`) | CI gate |

---

## 2. ASIL-D Gap Analysis (ISO 26262 §7)

c-RCP targets **ASIL-B** as its baseline, with four hazards (H-001,
H-003, H-005, H-008 — see `HARA.md`) computing to ASIL-C/D under ISO
26262-3:2018 Table 4. The following table records this project's own
current derogation posture, derived from the TC18 mechanisms Phases
13–21 actually built, not ported from any sibling project (this
project stopped mirroring cpp-RCP/go-RCP/rust-RCP at Phase 13):

| ASIL-D Requirement | Derogation Rationale | Current Coverage |
|--------------------|----------------------|-------------------|
| Redundant safety-tagged-request delivery paths | Not pursued at ECU boundary for ASIL-B | Single channel with the E2E CRC32 safe-point mechanism (`e2e.c`) and per-stream watchdog; no server-redundancy concept exists in TC18 (`redundancy.h`/`redundancy.c` were DEPRECATE-removed at milestone 83 — an RC Server is a single node with one lifecycle state) |
| Link-layer authentication (MACsec) | Explicitly out of this library's own scope — a link-layer, product-specific/opaque control the spec delegates to the deployment | **Not implemented in this library** — see `HARA.md` H-007/`tara.md` TS-004. This is an open item, not a process-rigor derogation on top of an implemented control. |
| Replay/staleness detection | The retired CRC-16 sequence-counter/replay-window mechanism has no TC18 counterpart in this codebase (`include/rcp/e2e.h`'s own file header records this explicitly) | **Not implemented in this library** — see `HARA.md` H-004/`tara.md` TS-002. Also an open item, not a derogation. |
| Formal proofs of absence of deadlock | TLA+ liveness proofs deferred to an ASIL-C/D upgrade path | TLC exhaustive model check on bounded state spaces (`tla/`, `FORMAL_VERIFICATION.md`) covering the lifecycle FSM and the E2E safe-point/watchdog mechanism |
| MISRA C:2012 mandatory + required compliance | Advisory rules selectively noted | `cfusa lint` clean on mandatory/required rules |
| 100% MC/DC structural coverage | Branch coverage is captured (`lcov --rc branch_coverage=1`, both `ci.yml`'s `coverage` job and `release.yml`); MC/DC itself is not separately instrumented | See §3 |

Unlike the ASIL-D-requirement rows above (a deliberate, reasoned choice
to not pursue a higher rigor level for an already-implemented
mechanism), H-004 and H-007's "Not implemented in this library" rows
are genuinely open gaps this re-certification pass surfaced rather than
closed — recorded honestly here rather than folded into the same
"derogation" framing as the others, which would misrepresent an absent
control as a considered rigor trade-off.

---

## 3. Structural Coverage Report

Coverage is measured by `cfusa coverage` against this project's own
`coverage.info` (LCOV), regenerated on every tagged release — see
`coverage-report.json`. As of `release.yml`'s current configuration,
both the CI `coverage` job and the release regeneration job pass `--rc
lcov_branch_coverage=1 --rc branch_coverage=1` to `lcov`, so branch data
is captured in `coverage.info` (this closes a gap the pre-TC18
`AUDIT_PACK.md` recorded as an open item — that configuration is no
longer accurate as of the workflow's current state and is not carried
forward here). Exact line/function/branch percentages are whatever
`coverage-report.json` currently reports after this milestone's release
regeneration — reported there rather than hand-copied into this
document, so this document cannot go stale relative to the actual
measured number the way copying a snapshot would.

MC/DC (Modified Condition/Decision Coverage) is not separately
instrumented — DO-178C DAL-B applicability (§4) notes this as an open
item, not resolved by branch coverage alone.

---

## 4. DO-178C (DAL-B) Applicability

If c-RCP is used in an airborne system under DO-178C DAL-B:

- Source code traceability to LLR: via `//cfusa:req` annotations —
  `.fusa-reqs.json`'s `scope: "tc18"` subset is this project's actual
  LLR basis; `scope: "legacy-compat"` entries are informational only
  (see §1)
- Tool qualification: `cfusa` is a Tool Qualification Level analysis
  tool — see `qualify-report.json`
- Decision coverage: MC/DC required at DAL-B — see the open item in §3
- Structural coverage artifacts: `coverage-report.json`, regenerated
  every tagged release
- Gap report: `do178-gap-report.json` (auto-generated, `cfusa do178 --dal b`)

---

## 5. CI Gate Summary

All of the following gates run on every tagged release
(`.github/workflows/release.yml`) or every PR (`.github/workflows/ci.yml`):

| Gate | Tool | Threshold |
|------|------|-----------|
| Static analysis | `cfusa check` | Zero errors |
| Lint | `cfusa lint` | Zero mandatory violations |
| MISRA/safety analysis | `cfusa analyze` | Zero safety violations |
| Cyber review | `cfusa cyber` | Zero cyber violations |
| Requirement coverage | `cfusa trace --req-coverage 100` | Both metrics (requirement traceability, function annotation density) = 100% |
| Formal verification | TLC model checking, `tla/LifecycleStateMachine.tla` + `tla/E2ESafePoint.tla` | No property violation |
| ASIL qualification | `cfusa qualify` | Qualified |
| Vulnerability scan | `cfusa vuln` | No known-vulnerable patterns |
| Safety case | `cfusa safety-case --gsn` | Generated every release |
| ISO 26262 report | `cfusa iso26262 --asil ASIL-B` | Gap report generated |
| IEC 61508 report | `cfusa iec61508 --sil SIL-2` | Gap report generated |
| DO-178C report | `cfusa do178 --dal b` | Gap report generated |
| IEC 62443 report | `cfusa iec62443 --sl SL-2` | Gap report generated |
| Coverage | `cfusa coverage` | Line/function/branch reported (see §3) |
| SCI (Software Change Impact) | `cfusa sci` | Generated every release |
| Audit pack | `cfusa audit-pack` | Generated (`audit-pack.zip`) |
| Release badge | `cfusa badge` | Generated |

---

## 6. Traceability Matrix

Requirements → implementation tracing is maintained in `.fusa-reqs.json`
(854 requirements: 779 `scope: "tc18"` covering the register-map,
lifecycle FSM, E2E safe points, every endpoint type's request/response
shape, discovery, power-mode transitions, and every ADAPT-class
satellite shipped through v0.84.0; 75 `scope: "legacy-compat"`
describing the retired pre-TC18 Zone/Command surface, kept — not
deleted — because `src/rcp.c`/`include/rcp/rcp.h` and
`tests/legacy_mock.*` still carry `//cfusa:req` tags naming them, per
`ROADMAP.md`'s v0.84.0 milestone confirming that surface as the last
consumer of those retired types anywhere in `src/`). `cfusa trace
--req-coverage 100` validates both metrics at 100% in CI: Metric 2
(function-annotation density) has been a hard gate since v0.1.0; Metric
1 (per-requirement traceability) became a hard gate at v0.53.0 once
every forward-declared requirement from early scaffolding was
implemented, and remains one — the current advisory `UNTRACED` list
(`REQ-MDNS-007/008`, `REQ-RELAY-013`) is unchanged by this milestone.

Implementation → test tracing: each `//cfusa:req` annotation in a source
file maps to one or more `//cfusa:test` annotations in `tests/`.

---

## 7. Change Impact Procedure

For any change to a safety-relevant source file:
1. Run `cfusa impact` to generate the change impact report
2. Review all impacted requirements in the SCI report (`sci.json`)
3. Re-run regression tests for all affected modules (`ctest`)
4. Update `.fusa-reqs.json` if the change introduces new requirements —
   set `scope: "tc18"` for anything describing shipped TC18 behavior;
   `scope: "legacy-compat"` is reserved for the retired pre-TC18
   surface and should not gain new entries
5. Re-generate the audit pack with `cfusa audit-pack`
6. Obtain safety team review approval before merging

---

## 8. Relationship to Earlier Milestones

This document's v1.0.0 (Milestone 43) mirrored cpp-RCP's own
`AUDIT_PACK.md` structure and ASIL-D derogation rationale, reporting on
the pre-TC18 Zone/Command protocol. As of Phase 13 (`ROADMAP.md`'s
Protocol Replacement Notice), c-RCP stopped mirroring cpp-RCP
port-for-port; this v2.0.0 revision reports on c-RCP's own TC18
implementation and its own measured/derived evidence, not a ported
figure from any sibling project.
