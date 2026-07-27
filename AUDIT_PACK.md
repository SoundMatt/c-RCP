# Audit Pack — c-RCP Certification Evidence (Milestone 43)

**Document version**: 1.0.0
**Standards**: ISO 26262 (ASIL-B), IEC 61508 (SIL-2), ISO 21434, IEC 62443 SL-2

---

## 1. Document Index

| Document | Location | Status |
|----------|----------|--------|
| HARA (Hazard Analysis & Risk Assessment) | `HARA.md` | Complete |
| TARA (Threat Analysis & Risk Assessment) | `tara.md` / `tara.json` | Complete (auto-generated, `cfusa tara`) |
| Cybersecurity Architecture | `CYBERSECURITY.md` | Complete |
| Formal Verification | `FORMAL_VERIFICATION.md` + `tla/*.tla` | Complete |
| Portability Audit | `PORTABILITY.md` | Complete |
| Safety Requirements | `.fusa-reqs.json` | 314 requirements across 44 groups |
| Safety Case | `safety-case.md` (auto-generated, `cfusa safety-case --gsn`) | CI gate |
| Release Badge | `fusa-badge.svg` (auto-generated, `cfusa badge`) | CI gate |

---

## 2. ASIL-D Gap Analysis (ISO 26262 §7)

c-RCP targets **ASIL-B** for the zone communication subsystem, matching
cpp-RCP's own target. The following table records deliberate derogations
from ASIL-D, ported from cpp-RCP's own gap analysis since the underlying
architecture (single-channel zonal network with E2E protection) is
identical between both projects:

| ASIL-D Requirement | Derogation Rationale | ASIL-B Coverage |
|--------------------|----------------------|-----------------|
| Redundant communication paths | Not required at ECU boundary for ASIL-B | Single channel with E2E protection (`e2e.c`); optional primary/standby via `rcp_redundancy_controller_new()` |
| Formal proofs of absence of deadlock | TLA+ liveness proofs deferred to ASIL-C/D upgrade path | TLC exhaustive model check on bounded state space (`tla/`, `FORMAL_VERIFICATION.md`) |
| MISRA C:2012 mandatory + required compliance | Advisory rules selectively noted; `CFUSA-L004` false-positive tracked as `SoundMatt/c-FuSa#59` | `cfusa lint` clean on mandatory/required rules |
| 100% MC/DC structural coverage | Branch coverage enforced in CI; MC/DC not separately instrumented | See §3 — real, measured coverage below |

ASIL decomposition: the zonal network is decomposed as ASIL-B(D) =
ASIL-A + ASIL-B per ISO 26262-9 §5 (independent channel decomposition).

---

## 3. Structural Coverage Report

Coverage is measured by `cfusa coverage` against this project's own
`coverage.info` (LCOV), regenerated on every tagged release — see
`coverage-report.json`. Unlike cpp-RCP's `AUDIT_PACK.md`, which lists
fabricated-looking per-module percentages, this section reports the
**actual measured, whole-project** numbers from the current
`coverage-report.json` rather than inventing a per-file breakdown `cfusa
coverage` doesn't produce:

| Metric | Measured | Threshold | Status |
|--------|----------|-----------|--------|
| Line coverage | 83.33% (3489/4187) | ≥ 80% (DAL-B) | Meets threshold |
| Function coverage | 86.92% (432/497) | — | Reported |
| Branch coverage | Not instrumented (0/0) | ≥ 80% (DAL-B target) | **Open item** |

**Open item**: this project's `lcov`/`gcov` invocation in
`.github/workflows/release.yml` does not currently capture branch data
(`0/0` rather than a real ratio), so branch and MC/DC coverage cannot yet
be reported honestly. Closing this gap — passing `--rc branch_coverage=1`
to `lcov` and `-b` (or `--coverage`) consistently through the build —
is tracked as follow-up work rather than papering over it with an
invented number.

---

## 4. DO-178C (DAL-B) Applicability

If c-RCP is used in an airborne system under DO-178C DAL-B:

- Source code traceability to LLR: via `//cfusa:req` annotations
- Tool qualification: `cfusa` is a Tool Qualification Level analysis
  tool — see `qualify-report.json` (22/22 checks passed)
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
| Static analysis | `cfusa check --strict` | Zero errors |
| Lint | `cfusa lint` | Zero mandatory violations |
| MISRA/safety analysis | `cfusa analyze` | Zero safety violations |
| Cyber review | `cfusa cyber` | Zero cyber violations |
| Requirement coverage | `cfusa trace --req-coverage 100` | Metric 2 (function annotation density) = 100% |
| Formal verification | `cfusa verify` | Verified |
| ASIL qualification | `cfusa qualify` | Qualified (22/22) |
| Vulnerability scan | `cfusa vuln` | No critical/high CVEs |
| Safety case | `cfusa safety-case --gsn` | Complete |
| ISO 26262 report | `cfusa iso26262 --asil ASIL-B` | Gap report generated |
| IEC 61508 report | `cfusa iec61508 --sil SIL-2` | Gap report generated |
| DO-178C report | `cfusa do178 --dal b` | Gap report generated |
| IEC 62443 report | `cfusa iec62443 --sl SL-2` | Gap report generated (v0.42.0) |
| Coverage | `cfusa coverage` | ≥ 80% line (DAL-B); branch — see §3 open item |
| SCI (Software Change Impact) | `cfusa sci` | Generated every release |
| Audit pack | `cfusa audit-pack` | Generated (`audit-pack.zip`) |
| Release badge | `cfusa badge` | Generated |

---

## 6. Traceability Matrix

Requirements → implementation tracing is maintained in `.fusa-reqs.json`
(314 requirements across 44 groups — every protocol bridge, decorator,
and platform primitive shipped from v0.1.0 through v0.42.0).
Traceability is validated by `cfusa trace --req-coverage 100` in CI:
Metric 2 (function-annotation density, a hard gate) is 100%; Metric 1
(per-requirement traceability) is tracked but non-blocking, since a
number of forward-declared requirements from early scaffolding
(`REQ-FI-008`, `REQ-AUTH-005`, `REQ-AUTH-008`, `REQ-MDNS-007/008`,
`REQ-PQ-005`, `REQ-RL-006`) describe behavior delegated to an inner
controller or a distinct error code rather than a directly-testable
code path — see the `trace` output for the current, exact list.

Implementation → test tracing: each `//cfusa:req` annotation in a source
file maps to one or more `//cfusa:test` annotations in `tests/`.

---

## 7. Change Impact Procedure

For any change to a safety-relevant source file:
1. Run `cfusa impact` to generate the change impact report
2. Review all impacted requirements in the SCI report (`sci.json`)
3. Re-run regression tests for all affected modules (`ctest`)
4. Update `.fusa-reqs.json` if the change introduces new requirements
5. Re-generate the audit pack with `cfusa audit-pack`
6. Obtain safety team review approval before merging

---

## 8. Relationship to cpp-RCP

This document mirrors cpp-RCP's own `AUDIT_PACK.md` structure and ASIL-D
derogation rationale (the underlying architecture is identical), but
reports c-RCP's own measured coverage numbers, requirement count, and
tool names (`cfusa` rather than `cpfusa`) rather than copying cpp-RCP's
figures verbatim — consistent with this project's practice throughout
the roadmap of verifying real numbers rather than asserting unverified
ones (see also `PORTABILITY.md` §"Conclusion" and the coverage open
item in §3 above).
