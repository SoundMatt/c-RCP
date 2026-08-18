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
| Safety Requirements | `.fusa-reqs.json` | 1095 requirements across four `scope` values — `tc18` (947: 859 ASIL-B/30 ASIL-A/58 QM, this project's ISO 26262 safety-case basis), `tc18-gap` (136: TC18 normative clauses not implemented, pinned rather than omitted), `retired` (6: superseded/citation-corrected entries), `internal` (6, QM: the allocator-hook indirection) — see `SEOOC_BOUNDARY.md` §4 for the freedom-from-interference argument over this partition, and the file's own `catalogNote` for the audit history |
| Safety Case | `safety-case.md` (auto-generated, `cfusa safety-case --gsn`) | CI gate |
| Release Badge | `fusa-badge.svg` (auto-generated, `cfusa badge`) | CI gate |
| SEOOC Boundary & Assumptions of Use | `SEOOC_BOUNDARY.md` | Added c-RCP-16 (issue #518) — see §2a |
| Tool Qualification Evidence | `qualify-report.json` (auto-generated, `cfusa qualify`) | CI gate — self-qualified, see §2a |

---

## 2. ASIL-C Gap Analysis (ISO 26262 §7)

**Audience and framing (c-RCP-16, issue #518, item 5):** this section
is this project's own internal derogation table against its own
ASIL-B/C baseline — it records why each ASIL-D-tier practice below
either was or wasn't pursued, for this repository's own contributors
and safety-case reviewers. It is evidence an integrator's own
item-level ASIL-D HARA (ISO 26262-3:2018 Clause 6) can *draw on*, not
a claim that c-RCP itself is ASIL-D-rated or ASIL-D-obligated — ASIL
attaches to a vehicle-level hazard reached through that item-level
HARA, never to a software element in isolation (`SEOOC_BOUNDARY.md`
§1). Concretely: an integrator whose own HARA assigns ASIL-D to a
hazard c-RCP participates in can read the "Current Coverage" column
below as *supporting* their own decomposition or undecomposed-rigor
argument at that ASIL, but the table does not do that argument's work
for them, and the "Not implemented" rows are exactly that — genuinely
absent controls, not conservatively-labeled present ones. See
`SEOOC_BOUNDARY.md` (added alongside §2a below) for the fuller
integrator-facing AoU/Safety-Manual-equivalent document this table is
one input to, and its §4 for this project's freedom-from-interference
argument over the ASIL/QM requirement-scope partition `.fusa-reqs.json`
records (§1 above; the numbers in that table's earlier "854
requirements / 75 legacy-compat" framing described a since-superseded
milestone snapshot and are corrected there).

c-RCP targets **ASIL-B** as its baseline. Under ISO 26262-3:2018 Table
4, only **one** hazard — H-001 (see `HARA.md`) — computes above that
baseline, to ASIL-C; no hazard computes to ASIL-D. (Prior to the
`cfusa` v0.5.50 CI pin bump, this section was titled "ASIL-D Gap
Analysis" and listed four hazards — H-001, H-003, H-005, H-008 — as
computing to ASIL-C/D; that was a direct consequence of a since-fixed
bug in `c-FuSa`'s shared ASIL derivation table, see `HARA.md`'s ASIL
Determination Note. H-003, H-005, and H-008 now land exactly at the
ASIL-B baseline and no longer require any derogation argument.) The
following table records this project's own current derogation posture
for H-001, derived from the TC18 mechanisms Phases 13–21 actually
built, not ported from any sibling project (this project stopped
mirroring cpp-RCP/go-RCP/rust-RCP at Phase 13); the higher-rigor
requirement rows below (redundancy, formal deadlock-absence proofs,
100% MC/DC) are ASIL-D-specific items retained here as this project's
stated stretch posture, not requirements H-001's actual ASIL-C rating
obligates:

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

## 2a. SEOOC Framing & Tool Confidence Level

c-RCP is developed and safety-argued as a Safety Element out of
Context (ISO 26262-10:2018 Clause 9). `SEOOC_BOUNDARY.md` (added
c-RCP-16, issue #518) is the integrator-facing Item Definition
boundary statement and consolidated Assumptions of Use (AoU) document
this implies — it collects, without duplicating, the assumptions
previously scattered across `safety-case.md`'s GSN node A1,
`HARA.md`'s Residual Risks table, and `tara.md`'s TS-001/TS-004 notes.
This §2's ASIL-C Gap Analysis table above records this project's own
derogation posture against its own ASIL-B baseline; `SEOOC_BOUNDARY.md`
is the separate, integrator-facing document that frames the same
evidence as *supporting, without asserting*, an integrator's own
item-level ASIL-D HARA — the two documents serve different audiences
and are not redundant with each other.

`SEOOC_BOUNDARY.md` §3 also documents `cfusa`'s own Tool Confidence
Level (TCL) posture: `qualify-report.json` records a **self-run,
non-independent** qualification (`qualificationMethod: "self"`), which
per `cfusa qualify`'s own ISO 26262-8:2018 Clause 11 TD/TI-derived
ceiling logic supports an ASIL-B tool-confidence argument at most. An
integrator relying on `cfusa`'s analysis output (not merely c-RCP's
requirement/test artifacts) as part of an ASIL-C/D safety case must
separately establish a higher TCL for `cfusa` themselves; c-RCP cannot
unilaterally qualify a tool shared across the whole x-RCP ecosystem.
(This corrects a prior regression: `ci.yml`/`release.yml`'s `cfusa
qualify` invocations had drifted to omitting `--qualification-method`
entirely, producing a self-contradictory `qualify-report.json`
[`qualified: true` beside `qualificationBadge: "unqualified"`] —
fixed in the same c-RCP-16 revision that added this section.)

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
  LLR basis; `scope: "tc18-gap"`/`"retired"`/`"internal"` entries are
  informational only (see §1; the pre-TC18 `scope: "legacy-compat"`
  surface this bullet previously referenced was fully removed at
  v0.91.0 and no longer exists in either the code or the catalog)
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
(1095 requirements: 947 `scope: "tc18"` covering the register-map,
lifecycle FSM, E2E safe points, every endpoint type's request/response
shape, discovery, power-mode transitions, and every ADAPT-class
satellite; 136 `scope: "tc18-gap"` recording TC18 normative clauses
not implemented, pinned by a test rather than omitted; 6
`scope: "retired"` superseded/citation-corrected entries; 6
`scope: "internal"` covering the allocator-hook indirection —
see §1 and `SEOOC_BOUNDARY.md` §4. The pre-TC18 `scope: "legacy-compat"`
Zone/Command surface this paragraph previously described was removed
from both `src/` and this catalog at v0.91.0; `tests/legacy_mock.*`
no longer exists). `cfusa trace
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
   set `scope: "tc18"` for anything describing shipped TC18 behavior,
   `scope: "tc18-gap"` for a normative clause deliberately not
   implemented, or `scope: "internal"` for an implementation-detail
   API with no direct TC18-spec basis (see §1); `scope: "retired"` is
   reserved for superseded/citation-corrected entries and should not
   gain new entries describing shipped functionality
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
