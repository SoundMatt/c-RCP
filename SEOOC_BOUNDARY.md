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
  lifecycle configuration gating, and the WakeUp handshake completion
  gate. Nothing else in this codebase should be read as an implied or
  aspirational safety mechanism. In particular, replay/staleness
  detection (H-004/SG-004) and link-layer authentication (H-007/SG-007)
  are **not implemented** anywhere in this library — see §2 below.
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
| AoU-3 | Replay/staleness detection for captured-and-resent requests is supplied by the deployment or a higher layer if the integration's own risk assessment requires it. c-RCP does not reimplement the retired CRC-16 sequence-counter/replay-window mechanism; the TC18 CRC32 safe-point mechanism that replaced it is an integrity check, not a freshness check, and does not close this gap. | `HARA.md` Residual Risks (H-004), `tara.md` TS-002 |
| AoU-4 | The first claimant to arrive at a server's reserved discovery `byte_bus_id` during `HW_UNCONFIGURED` is not cryptographically authenticated by c-RCP itself. An integrator whose threat model includes a rogue bootstrap claimant (`tara.md` TS-003) must supply that authentication at the link layer — the same control AoU-2 already requires for authenticated traffic in general. | `tara.md` TS-003 residual-risk note |
| AoU-5 | `rcp_e2e_endpoint_in_safe_state()` fails closed (returns "not safe") on a misconfigured `safestate_sequencer` index or an unrecognized `rx_safety_measure` value, by explicit design choice rather than a spec mandate. An integrator must confirm this fail-closed posture is what their own safe-state definition actually requires before relying on it, rather than assuming a spec-mandated behavior. | `HARA.md` Residual Risks table |
| AoU-6 | c-RCP's safety mechanisms are analyzed and tested at c-RCP's own implementation-correctness level; no separate ASIL decomposition argument (ISO 26262-9:2018 Clause 5) has been constructed for H-001 (the sole element-level hazard exceeding the ASIL-B baseline, at ASIL-C). An integrator whose item-level HARA assigns an ASIL to the corresponding vehicle-level hazard must construct their own decomposition or undecomposed-rigor argument at that ASIL — c-RCP's own ASIL-C rating does not do this for them. | `HARA.md` ASIL Determination Note |
| AoU-7 | c-RCP's own tool confidence evidence for `cfusa` (its static-analysis/lint/traceability toolchain) is a **self-run, non-independent qualification** (`qualify-report.json`, `qualificationMethod: "self"`) — see §3. An integrator targeting an ASIL where ISO 26262-8:2018 Clause 11 would require a higher tool confidence level than self-qualification provides must independently qualify `cfusa` (or substitute their own toolchain) to that level; c-RCP's CI evidence does not satisfy that requirement on its own. | §3 below; `qualify-report.json` |
| AoU-8 | `rcp_alloc_set_hooks()` (`alloc.c`) is public API with no ASIL/scope gate: it overwrites one process-wide, unsynchronized global hook table that every allocation in the library — QM and ASIL-B/A alike, across 46 of c-RCP's `src/*.c` files — routes through via `rcp_malloc()`/`rcp_calloc()`/`rcp_realloc()`/`rcp_free()`. An integrator must treat any call to `rcp_alloc_set_hooks()` anywhere in their own application, QM-rated call sites included, as safety-relevant with respect to every ASIL-rated code path in c-RCP — c-RCP itself does not restrict, gate, or synchronize who may call it. See §4 below. | §4 below; `alloc.c` |

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

## 4. Freedom from interference (ISO 26262-6:2018 Clause 7 / -9:2018 Clause 6)

c-RCP is a pure-C99 library built as one statically-linked archive
(`add_library(rcp STATIC ...)`, `CMakeLists.txt`) — every consumer
links a single binary containing every requirement scope tier
together. c-RCP provides **no OS-level partition of its own** (no
process boundary, no memory-protection domain, no separate
compilation unit per ASIL tier): if freedom from interference between
QM and ASIL-rated code is required, the integrator's own architecture
(a partitioned OS, MPU/MMU regions, or separate processes) must supply
it — this is itself an Assumption of Use this section makes explicit
where the previous AoU-1 (hardware/platform safety) left it implicit.

**Current requirement-scope partition** (`.fusa-reqs.json`, verified
against current HEAD — see that file's `catalogNote` for the full
audit history): 1095 requirements across four `scope` values —
`tc18` (947: 859 ASIL-B, 30 ASIL-A, 58 QM — the shipped, TC18-spec-derived
behavior), `tc18-gap` (136: 10 ASIL-B, 126 QM — TC18 normative clauses
c-RCP does *not* implement, pinned by a test rather than omitted
silently), `retired` (6 — superseded/citation-corrected requirement
entries kept for traceability, not shipped functionality of their
own), and `internal` (6, all QM — the pluggable allocator-hook
indirection in `alloc.c`, an implementation-detail API with no direct
TC18-spec basis). Note for readers of issue #518 itself: the issue's
own framing (`scope: "tc18"` vs. a QM `scope: "legacy-compat"` retired
Zone/Command surface "still linked into the same binary per
`src/rcp.c`/`tests/legacy_mock.*`") is now stale and the specific
concern it raised is moot — verified against current HEAD, that
pre-TC18 object model (`rcp_zone_t`/`rcp_command_t`/`rcp_response_t`/
etc.) was fully removed at v0.91.0 (see `CHANGELOG.md`'s Deprecation &
Removal Log); `src/rcp.c` is now 65 lines of generic error-string and
byte-buffer helpers with no Zone/Command types in it, and
`tests/legacy_mock.*` no longer exists anywhere in this tree. The
`legacy-compat` scope value itself no longer appears in
`.fusa-reqs.json` at all.

**The QM/ASIL-rated boundary is function-level, not file-level.**
`scope: "tc18"`'s 58 QM entries are not confined to otherwise-QM
files — `src/lifecycle.c` alone carries ASIL-A, ASIL-B, *and* QM
`//cfusa:req` tags in the same translation unit (confirmed by direct
grep of its `//cfusa:req` tags against `.fusa-reqs.json`'s `level`
field). c-RCP provides no compiler-enforced or file-boundary
separation between a QM-rated helper function and an ASIL-B-rated one
sitting beside it in the same `.c` file; an integrator reasoning about
freedom from interference at the source level must do so
function-by-function against `.fusa-reqs.json`'s `id`/`level`
pairing, not file-by-file.

**The one concrete, cross-cutting interference vector this analysis
found:** `alloc.c`'s allocator-hook indirection (`REQ-ALLOC-001..006`,
`scope: "internal"`, all QM). `rcp_malloc()`/`rcp_calloc()`/
`rcp_realloc()`/`rcp_free()` are used across 46 of c-RCP's `src/*.c`
files — effectively the entire ASIL-rated surface — and every one of
them dereferences a single process-wide, unsynchronized static struct
of function pointers (`g_hooks`). `rcp_alloc_set_hooks()`, the
function that overwrites that struct, is unrestricted public API: it
carries no ASIL/scope gate, no access check, and no synchronization
against a concurrent `rcp_malloc()`/etc. call already in flight. A
QM-rated call site anywhere in an integrator's own application —
c-RCP itself never calls `rcp_alloc_set_hooks()` internally, per a
repo-wide search — can silently redirect every subsequent allocation
across the entire library, ASIL-B TC18 code paths included, to a
custom allocator of unknown provenance and unknown correctness. This
is now recorded as AoU-8 (§2): an integrator must treat any call to
`rcp_alloc_set_hooks()` in their own codebase as safety-relevant with
respect to every ASIL-rated path in c-RCP, regardless of which
ASIL/QM tier the calling code itself carries. c-RCP does not close
this gap on the integrator's behalf — no gate, wrapper, or
access-control mechanism restricting `rcp_alloc_set_hooks()` exists in
this codebase today, and none is claimed.

## 5. Safety-relevant interface contract (pointer)

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

## 6. What this document does not do

Consistent with issue #518's own scoping: this document does not
assign ASIL-D to c-RCP (a category error for an SEOOC — ASIL attaches
to a vehicle-level hazard via an integrator's own item-level HARA, not
to a software element in isolation), does not close H-004 or H-007
(both remain genuinely open per AoU-2/AoU-3 above), does not
re-litigate `HARA.md`'s ASIL letters, and does not introduce any dual
independent human-review/overcheck process gate — organizational
redundancy measures of that kind are explicitly out of scope for
c-RCP-16 and belong in a separate issue if wanted.

---
_Document owner: SoundMatt/c-RCP maintainers_
_Review date: on next HARA/TARA/safety-mechanism change, or annually,
whichever is sooner — same cadence as `tara.md`_
_Standard: ISO 26262-10:2018 Clause 9_
