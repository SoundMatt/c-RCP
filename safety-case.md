# Safety Case — c-RCP v0.97.0

**Standard:** iso26262  |  **Generated:** 2026-07-30T17:17:24Z

---

## G1 — goal

> c-RCP v0.97.0 has no unmitigated hazard from .fusa-hara.json and no unresolved ERROR finding from `cfusa check` at the iso26262 analysis boundary

## St1 — strategy

> Argue over hazard elimination and process confidence separately

## C1 — context

> Scope: c-RCP source under ".", analyzed against iso26262 by c-FuSa v0.5.48

## A1 — assumption

> The underlying hardware/platform on which c-RCP runs meets its own safety requirements independently of this software safety case

## G1.1 — goal

> Every hazard recorded in .fusa-hara.json is eliminated or controlled to its assigned ASIL (ISO 26262-3 Clause 6)

## G1.2 — goal

> The c-RCP development process gives justified confidence: static analysis, FMEA/TARA, and tool qualification evidence are current

## Sn3 — solution

> Design FMEA

Evidence: `fmea.json`

## Sn4 — solution

> Threat analysis and risk assessment

Evidence: `tara.json`

## Sn5 — solution

> Tool qualification record

Evidence: `qualify-report.json`

---

## Argument Structure

| From | To | Relation |
|---|---|---|
| G1 | St1 | supportedBy |
| G1 | C1 | inContextOf |
| St1 | A1 | inContextOf |
| St1 | G1.1 | supportedBy |
| St1 | G1.2 | supportedBy |
| G1.2 | Sn3 | supportedBy |
| G1.2 | Sn4 | supportedBy |
| G1.2 | Sn5 | supportedBy |

_Completeness: 3 goal(s), 1 with cited evidence, 1 undeveloped._
