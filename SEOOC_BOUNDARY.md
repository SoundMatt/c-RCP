# SEOOC Boundary & Assumptions of Use — c-RCP

**Standard basis:** ISO 26262-10:2018 Clause 9 (Safety Element out of Context)
**Applies to:** c-RCP vX.Y.Z, the pure-C99 implementation of the OPEN
Alliance TC18 Remote Control Protocol
**Audience:** an integrator performing their own item-level HARA
(ISO 26262-3:2018 Clause 6) over a vehicle function that uses c-RCP —
not this repository's own contributors, who should keep reading
`SAFETY_PLAN.md`/`HARA.md`/`tara.md` as the primary internal record

This document did not exist before c-RCP-16 (issue #518). Before it,
the individual facts below existed but were scattered across
`safety-case.md`'s GSN node A1, `HARA.md`'s Residual Risks table, and
`tara.md`'s TS-001/TS-004 notes — this document is the first place
that consolidates them into the single, integrator-facing shape ISO
26262-10:2018 Clause 9.3 expects of an SEOOC's Assumptions of Use. It
supersedes nothing; every underlying fact it restates remains the
source-of-record in the document that already stated it, and this file
is updated whenever any of those change.

`FREEDOM_FROM_INTERFERENCE.md` (added c-RCP-16 item 4) is this
document's companion for the QM/ASIL-A/B co-existence question ISO
26262-6:2018 Clause 7 / ISO 26262-9:2018 Clause 6 raises for a single
linked binary; AoU-8 below is its one substantive finding, restated
here for completeness rather than duplicated in full.

---

## 1. Item Definition boundary — what c-RCP does *not* own

c-RCP is a **Safety Element out of Context**: a software element
developed and safety-argued independent of any specific vehicle
program, integrated later into an item whose HARA and ASIL assignment
this repository has no visibility into (ISO 26262-10:2018 Clause 9.1).
Concretely, c-RCP does **not** provide, and its safety case does not
claim:

- **A vehicle-level HARA.** `HARA.md` in this repository is c-RCP's
  own *element-level* hazard analysis, scoped to hazards this library
  can itself cause or fail to mitigate (loss of a safety-tagged
  request, a watchdog that doesn't fire, an unauthorized register
  write, and so on — see `HARA.md`'s Operational Situations and Hazard
  Table). It is not, and cannot substitute for, the item-level HARA
  ISO 26262-3:2018 Clause 6 requires an integrator to perform once
  c-RCP is placed in an actual vehicle function with real actuators,
  real occupants, and a real operating environment.
- **An integration-level ASIL assignment.** The ASIL letters in
  `HARA.md` (ASIL-B baseline, one ASIL-C hazard, H-001) are this
  library's own element-level ratings, computed against c-RCP's
  hazards in isolation. ASIL is a property of a vehicle-level hazard
  reached through an item-level HARA (ISO 26262-3:2018 Clause 6), not
  a property a software element can assign to itself. An integrator's
  own item-level HARA may reach a different ASIL for the vehicle
  function c-RCP participates in — including ASIL-D — and c-RCP's own
  ASIL-B/C letters neither cap nor guarantee that outcome.
- **Any implemented safety mechanism beyond what `HARA.md` records.**
  The safety mechanisms table in `SAFETY_PLAN.md` is exhaustive as of
  this document's own revision: the per-stream watchdog/safe-state
  gate, CRC32 frame integrity, register-map write authorization,
  lifecycle configuration gating, the WakeUp handshake completion gate,
  and (as of 2026-08-20, issue #606/#601) the opt-in sequence-number
  replay/staleness gate (H-004/SG-004). Nothing else in this codebase
  should be read as an implied or aspirational safety mechanism. Two
  hazards need care here: replay/staleness detection (H-004/SG-004) is
  now **implemented, but opt-in** — see AoU-3 below for the conditions
  under which it actually applies; link-layer authentication
  (H-007/SG-007) remains **not implemented** anywhere in this library —
  see §2 below.
- **Hardware architectural metrics (SPFM/LFM/PMHF).** c-RCP is a pure
  C99 software library with no hardware element of its own; ISO
  26262-5:2018's hardware-element metrics are out of scope for this
  SEOOC's own evidence package by construction, not by omission.

## 2. Assumptions of Use (AoU)

Per ISO 26262-10:2018 Clause 9.3, an SEOOC's safety case is only valid
if the assumptions it was analyzed under actually hold once integrated.
The following AoU items are binding: an integrator relying on c-RCP's
safety case (`HARA.md`, `safety-case.md`, the safety mechanisms in
`SAFETY_PLAN.md`) must independently verify and satisfy every one of
them. Each restates a fact already recorded elsewhere in this repo;
the citation after each item is that fact's source-of-record.

| # | Assumption of Use | Source of record |
|---|---|---|
| AoU-1 | The underlying hardware/platform c-RCP runs on meets its own safety requirements independently of this software safety case (memory protection, clocking, power integrity, and any hardware-level fault detection this library does not itself provide). | `safety-case.md` GSN node A1 |
| AoU-2 | Link-layer authentication (MACsec, 802.1AE) is supplied by the deployment on any transport carrying safety-relevant requests. c-RCP's own transports (`udp.c`, `avtp.c`, `shmem.c`) carry AVTPDUs with no authentication or encryption layer of their own — this is a deliberate scope boundary (`tls.c` was deprecated and removed at v0.78.0 with MACsec as its designated replacement, itself out of this library's scope), not an oversight. | `HARA.md` Residual Risks (H-007), `tara.md` TS-001/TS-004 |
| AoU-3 | Replay/staleness detection for captured-and-resent requests is implemented in c-RCP as of 2026-08-20 (issue #606/#601) — TC18 §12.7.7 Table 24's own `rx_enforce_seq`/`rx_seq_safestate_enable` mechanism (`rcp_e2e_seq_evaluate()`/`rcp_e2e_seq_tracker_t`) — but it is **opt-in and does not apply automatically**. An integrator relying on it must: (1) enable `rx_enforce_seq`/`rx_seq_safestate_enable` via `regmap.h` for the relevant request stream; (2) replicate `mock.c`'s `frame_seq_gate_admits()` admission wiring (evaluate once per AVTPDU frame, before any ACF member is processed) in their own real, I/O-attached dispatch loop — c-RCP ships no networked dispatcher of its own, only a reference composition; and (3) accept the mechanism's documented residual risk: tracker state is per-process/per-restart (no persistence across restarts), RFC 1982 forward-window comparison only distinguishes a sequence gap in `[1,127]` from ordinary 8-bit wraparound, and detection granularity is per-AVTPDU-frame, not per-ACF-message. If an integrator's own risk assessment needs coverage beyond this scope (e.g. persistence across restarts, a wider replay window), that coverage remains the deployment's or a higher layer's responsibility. | `HARA.md` Residual Risks (H-004), `tara.md` TS-002, `include/rcp/e2e.h` file header |
| AoU-4 | The first claimant to arrive at a server's reserved discovery `byte_bus_id` during `HW_UNCONFIGURED` is not cryptographically authenticated by c-RCP itself. An integrator whose threat model includes a rogue bootstrap claimant (`tara.md` TS-003) must supply that authentication at the link layer — the same control AoU-2 already requires for authenticated traffic in general. | `tara.md` TS-003 residual-risk note |
| AoU-5 | `rcp_e2e_endpoint_in_safe_state()` fails closed (returns "not safe") on a misconfigured `safestate_sequencer` index or an unrecognized `rx_safety_measure` value, by explicit design choice rather than a spec mandate. An integrator must confirm this fail-closed posture is what their own safe-state definition actually requires before relying on it, rather than assuming a spec-mandated behavior. | `HARA.md` Residual Risks table |
| AoU-6 | c-RCP's safety mechanisms are analyzed and tested at c-RCP's own implementation-correctness level; no separate ASIL decomposition argument (ISO 26262-9:2018 Clause 5) has been constructed for H-001 (the sole element-level hazard exceeding the ASIL-B baseline, at ASIL-C). An integrator whose item-level HARA assigns an ASIL to the corresponding vehicle-level hazard must construct their own decomposition or undecomposed-rigor argument at that ASIL — c-RCP's own ASIL-C rating does not do this for them. | `HARA.md` ASIL Determination Note |
| AoU-7 | c-RCP's own tool confidence evidence for `cfusa` (its static-analysis/lint/traceability toolchain) is a **self-run, non-independent qualification** (`qualify-report.json`, `qualificationMethod: "self"`) — see §3. An integrator targeting an ASIL where ISO 26262-8:2018 Clause 11 would require a higher tool confidence level than self-qualification provides must independently qualify `cfusa` (or substitute their own toolchain) to that level; c-RCP's CI evidence does not satisfy that requirement on its own. | §3 below; `qualify-report.json` |
| AoU-8 | c-RCP's ASIL-B-rated E2E safe-point mechanism (`e2e.c`) allocates memory, on its actual per-request safety-relevant path, through a **single process-wide allocator-hook table** (`alloc.h`/`alloc.c`) that any QM-rated caller in the same process — including c-RCP's own QM-rated features — could, prior to [c-RCP-23b] (issue #600), redirect with no access control; the ASIL-B watchdog mechanism (`watchdog.c`) has the same dependency in a narrower form, once per keeper construction/destruction rather than per tick, since c-RCP-17 (issue #521) converted its per-stream/callback storage to fixed capacity. As of [c-RCP-23b], `rcp_alloc_lock_hooks()` lets an integrator lock the table (after installing whatever hooks their own startup sequence needs, before any ASIL-rated code path's first allocation) so that further `rcp_alloc_set_hooks()`/`rcp_alloc_reset_hooks()` calls are rejected — **this narrows, but does not retire, this AoU**: it requires no change to `e2e.c`/`watchdog.c` themselves and closes the access-control gap for an integrator who opts in, but the lock remains opt-in (c-RCP cannot call it on the integrator's own behalf), is a single global switch rather than a true per-ASIL-tier partition, and provides no attribution of an attempted override. An integrator who calls `rcp_alloc_set_hooks()` anywhere in their integration, or who never calls `rcp_alloc_lock_hooks()`, must independently ensure the installed hooks are trustworthy to the ASIL of the highest-rated call site they will service; c-RCP does not, and structurally cannot on its own, enforce a true QM/ASIL partition over this shared allocator. | `FREEDOM_FROM_INTERFERENCE.md` §2, §2.1 |

## 3. Tool confidence level (TCL) — `cfusa`

c-RCP's own CI gates (`cfusa check`/`lint`/`analyze`/`trace`) are
produced by `cfusa` (the `c-FuSa` project, pinned per
`.github/workflows/ci.yml`/`release.yml`). Per ISO 26262-8:2018 Clause
11, a tool whose own malfunction could fail to detect a safety-relevant
defect — which describes `cfusa`'s static-analysis and traceability
checks exactly — requires a Tool Confidence Level (TCL) determination
before its output can be relied on as safety evidence at a given ASIL.

`qualify-report.json` (regenerated by this revision; see the CHANGELOG
entry for the CI fix that produced it) now honestly records
`"qualificationMethod": "self"` and `"qualificationBadge":
"self-qualified"` — a self-run of `cfusa qualify`'s own known-answer
and rule-exercise test suite, with **no independent reviewer and no
independent test executor**. Per `cfusa qualify --help`'s own
documented ceiling logic (ISO 26262-8:2018 Clause 11 TD/TI-derived
determination): no independent reviewer caps the achievable ceiling at
ASIL-B; an independent reviewer alone reaches ASIL-C; an independent
reviewer *and* an independent test executor reach ASIL-D.

This has a direct, honest consequence for this SEOOC's evidence
package: **`cfusa`'s current qualification evidence supports an
ASIL-B tool-confidence argument at most.** An integrator whose
item-level HARA assigns ASIL-C or ASIL-D to a hazard this library
addresses, and who intends to rely on `cfusa`'s own analysis output
(not merely c-RCP's requirement/test artifacts, which are independent
of the tool's own correctness) as part of their safety case, must
separately establish a higher TCL for `cfusa` themselves — by
independent review of `cfusa`'s development process, an independent
test execution over `cfusa`'s own qualification suite, or a validation
suite of their own (ISO 26262-8:2018 Clause 11.4.8-11.4.9). c-RCP
cannot unilaterally qualify `cfusa` to a higher TCL on an integrator's
behalf: `cfusa` is a tool shared across the whole x-RCP ecosystem
(go-RCP, cpp-RCP, rust-RCP), not owned or independently reviewable from
within this repository alone.

This TCL argument is deliberately narrow: it concerns `cfusa`'s
*analysis and reporting* tools only. It does not extend to `cfusa`'s
role (if any) in a build's toolchain — c-RCP's actual build/test
toolchain (the C compiler, `ctest`, ASan/UBSan) is a separate TCL
question this document does not address.

## 4. Safety-relevant interface contract (pointer)

c-RCP's public interface surface is `include/rcp/*.h`; its
lexicon/file-path mapping against the shared x-RCP architecture is
`ARCHITECTURE.md`. This document does not restate that mapping. The
safety-relevant subset an integrator should focus on when reasoning
about the interface contract is the set of functions
`SAFETY_PLAN.md`'s safety mechanisms table names (`rcp_e2e_wd_evaluate()`,
`rcp_e2e_request_may_execute()`, `rcp_regmap_writer_ctx()`,
`rcp_pwrmode_handshake_is_complete()`, and the others listed there) —
each traces to the requirement IDs in `.fusa-reqs.json` that define its
expected pre/post-conditions. A fuller, dedicated safety-relevant
interface contract document (return-value/error-code safe-state
mapping, thread-safety/reentrancy contract, and the boundary behavior
each function exhibits on malformed input) is deferred to a future
revision of this document — see the c-RCP-16 (issue #518) tracking
comment for the current status of that remaining work.

## 5. What this document does not do

Consistent with issue #518's own scoping: this document does not
assign ASIL-D to c-RCP (a category error for an SEOOC — ASIL attaches
to a vehicle-level hazard via an integrator's own item-level HARA, not
to a software element in isolation), does not re-litigate `HARA.md`'s
ASIL letters, and does not introduce any dual independent
human-review/overcheck process gate — organizational redundancy
measures of that kind are explicitly out of scope for c-RCP-16 and
belong in a separate issue if wanted. It does not close H-007 (remains
genuinely open per AoU-2 above — MACsec is out of this library's scope
entirely). It does not *unconditionally* close H-004 either (updated
2026-08-20, issue #606/#601): a real, opt-in mitigation now exists (see
AoU-3), but an integrator who does not enable the config bits and
replicate the reference dispatch wiring in their own production loop
gets none of its benefit — H-004 is Mitigated (opt-in), not
unconditionally Closed.

---
_Document owner: SoundMatt/c-RCP maintainers_
_Review date: on next HARA/TARA/safety-mechanism change, or annually,
whichever is sooner — same cadence as `tara.md`_
_Standard: ISO 26262-10:2018 Clause 9_
