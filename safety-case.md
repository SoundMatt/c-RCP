# Safety Case — c-RCP v0.70.0

**Standard:** iso26262  |  **Generated:** 2026-07-28T19:28:56Z

---

## G1 — Top-Level Goal

> **c-RCP is acceptably safe for its intended use in its intended environment**

### Evidence for G1

| Evidence Item | Source | Status |
|---|---|---|
| Hazard analysis complete | hara.md | [ ] |
| Safety requirements defined | safety-plan.md | [ ] |
| MISRA-C lint clean | cfusa lint | [ ] |
| Static analysis clean | cfusa analyze | [ ] |
| Cybersecurity analysis complete | cfusa cyber / tara.md | [ ] |
| Test evidence complete | cfusa verify | [ ] |
| Coverage targets met | cfusa coverage | [ ] |
| FMEA complete | fmea.md | [ ] |
| Tool qualification | cfusa qualify | [ ] |
| SCI produced | cfusa sci | [ ] |

---

## G1.1 — Hazard Elimination

> **All identified hazards are either eliminated or controlled to an acceptable level**

- Context: C1 — Operating environment is [describe]
- Strategy: S1 — Argue over identified hazardous events

### G1.1 Sub-goals

| ID | Sub-goal | Evidence |
|---|---|---|
| G1.1.1 | Hazardous event HE-001 risk is [ASIL] or below | hara.md §2 |
| G1.1.2 | Software contributes no additional hazards | cfusa check (exit 0) |

---

## G1.2 — Process Confidence

> **The software development process is sufficient to give confidence in the result**

- S2 — Argue over process compliance

| ID | Sub-goal | Evidence |
|---|---|---|
| G1.2.1 | Coding standard followed | cfusa lint (exit 0) |
| G1.2.2 | Requirements traced to code | cfusa trace |
| G1.2.3 | Test coverage meets threshold | cfusa coverage |
| G1.2.4 | All PRs resolved | cfusa pr --status open |

---

## Assumptions

- A1: The hardware is assumed safe as per [HW safety case reference]
- A2: The operating environment is as described in [SRS reference]

---

_Safety case owner: [name / role]_  
_Last reviewed: [date]_  
_Next review: [date]_
