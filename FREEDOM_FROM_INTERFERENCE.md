# Freedom-from-Interference Argument — c-RCP

**Standard basis:** ISO 26262-6:2018 Clause 7 (Freedom from interference
between software components) / ISO 26262-9:2018 Clause 6 (Criteria for
coexistence)
**Applies to:** c-RCP vX.Y.Z, the pure-C99 implementation of the OPEN
Alliance TC18 Remote Control Protocol
**Audience:** an integrator performing their own item-level HARA over a
vehicle function that uses c-RCP, and this repository's own
contributors reasoning about the QM/ASIL partition inside a single
linked binary — see `SEOOC_BOUNDARY.md` for the wider SEOOC framing this
document is part of

This document exists because c-RCP's `.fusa-reqs.json` mixes
ASIL-A/ASIL-B-rated and QM-rated requirements that all link into the
**same binary** (`ASIL/QM co-existence` in ISO 26262-9:2018 Clause 6
terms), and no document in this repository previously reasoned about
whether the QM subset can corrupt the ASIL-rated subset it shares an
address space with. Issue #518 (c-RCP-16) item 4 asked for exactly that
argument.

---

## 1. What actually shares the binary today (verified against HEAD, not the issue's original framing)

Issue #518's own text describes the split as `.fusa-reqs.json` mixing
`scope: "tc18"` (ASIL-rated) against `scope: "legacy-compat"` (QM,
"the retired pre-TC18 Zone/Command surface, still linked into the same
binary per `src/rcp.c`/`tests/legacy_mock.*`"). That framing is now
**stale** — `.fusa-reqs.json`'s current `scope` values (verified by
direct enumeration of all 1267 entries at HEAD, after the [c-RCP-16
follow-up] issue #552 fix and the [c-RCP-18-tracker] issue #533 Group
3/Group 1/Group 2 (REQ-ACF-*, REQ-AVTP-*, REQ-RMAP-*, REQ-SPI-*,
REQ-ADC-*, REQ-GPIO-*, REQ-LINEP-*, REQ-ISELED-*, REQ-CANEP-*, REQ-UART-*, REQ-PWM-*, REQ-MDIO-*, REQ-WAKEUP-*) requirement-atomicity splits) are:

| `scope` | Count | ASIL mix | What it actually is today |
|---|---|---|---|
| `tc18` | 1229 | 1114 ASIL-B, 36 ASIL-A, 79 QM | The shipped TC18 behavior. The 79 QM-rated entries inside this scope are optional/non-safety-relevant TC18 features (e.g. discovery cosmetics) implemented alongside the ASIL-rated core, not a separate module. |
| `tc18-gap` | 23 | 23 QM, 0 ASIL-B | Catalog markers for TC18 normative clauses this implementation does not fully meet. `tc18-gap`'s own catalog note says this scope should always be QM, and, as of the [c-RCP-16 follow-up] issue #548 pass (§4), that invariant now actually holds: every remaining entry is QM-rated. The `REQ-CANEP-*`/`REQ-PWM-*` batches' own splits each added entries here (`REQ-CANEP-038`; `REQ-PWM-068`/`-069`), moving this scope from 20 to 23; the `REQ-MDIO-*`/`REQ-WAKEUP-*` batches' own splits added no new entries, leaving that scope unchanged at 23. |
| `retired` | 9 | 6 ASIL-B, 1 ASIL-A, 2 QM | Dead requirement-catalog text kept only because a surviving `//cfusa:req` tag still cites the ID (deleting the entry would create a dangling reference `cfusa trace` would flag) — not live code. See §3. Includes `REQ-ISELED-028` (moved here from `tc18-gap` by issue #552 -- its own text already said RETIRED, only the scope field hadn't caught up). |
| `internal` | 6 | 6 QM | The allocator-hook indirection layer (`alloc.h`/`alloc.c`) — infrastructure every module calls through, not a feature module of its own. See §2's main finding. |

The `tests/legacy_mock.*` file the issue cites no longer exists, and
`src/rcp.c` (67 lines at HEAD) is limited to `relay_strerror()`/
`rcp_strerror()` — the pre-TC18 Zone/Command object model the issue
describes was fully removed at v0.91.0 per `CHANGELOG.md`'s Deprecation
& Removal Log. There is no surviving "legacy Zone/Command surface"
sharing the binary today; that specific interference concern from the
issue's original framing is moot, not because it was mitigated but
because the code it was about no longer exists.

## 2. The real interference vector: the shared allocator-hook table

`include/rcp/alloc.h`/`src/alloc.c` (scope `internal`, QM-rated end to
end — `REQ-ALLOC-001`..`REQ-ALLOC-006`) implement `rcp_malloc()`/
`rcp_calloc()`/`rcp_realloc()`/`rcp_free()` as thin wrappers around a
**single process-wide, mutable global**:

```c
static rcp_alloc_hooks_t g_hooks;   /* src/alloc.c */
```

`rcp_alloc_set_hooks()` — a public function declared in the public
header `include/rcp/alloc.h` — overwrites this global unconditionally
for the whole process, with no access-control check, no per-caller
namespace, and no distinction between a QM-rated caller and an
ASIL-rated one. Every `rcp_malloc`/`rcp_calloc`/`rcp_realloc`/`rcp_free`
call anywhere in this codebase — QM or ASIL-rated — resolves through
this one shared hook table.

This matters because it is not purely QM-side infrastructure: this
project's ASIL-B safety mechanisms allocate through it, in two
different shapes:

- `src/e2e.c` (`REQ-E2E-*`, ASIL-B): `rcp_e2e_wrap()` and
  `rcp_e2e_unwrap()` call `rcp_malloc()` to build the CRC32-covered copy
  of the frame **on every wrap/unwrap of a safety-tagged request** —
  a genuinely per-request, hot safety-relevant execution path —
  `src/e2e.c:237` and `:316`.
- `src/watchdog.c` (`REQ-WDG-*`, ASIL-B): as of c-RCP-17's fixed-
  capacity conversion (issue #521, PR #538, landed concurrently with
  this document's own drafting and re-verified against it here after
  rebasing past that PR), the per-stream `states[]` table and the
  `callbacks[]`/`callback_ctx[]` lists are now fixed-size embedded
  arrays, no longer separately allocated or `rcp_realloc()`-grown. The
  watchdog keeper struct itself is still allocated **once**, via
  `rcp_calloc(1, sizeof(*k))` at `rcp_watchdog_keeper_new()`
  (`src/watchdog.c:160`) and freed once via `rcp_free()` at
  `rcp_watchdog_keeper_destroy()` (`src/watchdog.c:246`) — a
  construction/destruction-time dependency on the shared allocator, not
  a per-tick or per-request one.

**This is a genuine, undischarged freedom-from-interference gap, not
one this document can close from c-RCP's own side** — though the two
call sites differ in how exposed they are. A QM-rated caller anywhere
in an integrator's application — including c-RCP's own `tc18`-scope QM
features or a future `tc18-gap` implementation — can call
`rcp_alloc_set_hooks()` and redirect memory management for both
mechanisms to arbitrary, unqualified code, with no mechanism in this
library to detect or prevent it. `e2e.c`'s exposure is the more
significant of the two: a hostile or corrupted hook installed at any
point during operation corrupts every subsequent safety-tagged
request's CRC32 safe-point computation. `watchdog.c`'s is narrower
post-c-RCP-17: a hook only needs to behave correctly at the one
`rcp_watchdog_keeper_new()`/`_destroy()` call pair surrounding the
keeper's lifetime, not on every tick — still a real dependency, not a
zero one, but a smaller window than before this document's own
drafting started. Two further mitigating facts are worth recording
honestly rather than treated as closing the gap:

1. **The default (no hooks installed) passes straight through to
   libc's `malloc`/`calloc`/`realloc`/`free`** — a widely-used,
   independently-evaluated allocator, not bespoke unqualified code.
   The interference risk is conditional on an integrator (or a future
   contributor) actually installing a hook, not present by default.
2. **Every one of these call sites fails gracefully on allocation
   failure.** `rcp_e2e_wrap()` returns an empty/zero-length result if
   `rcp_malloc()` returns `NULL` (`src/e2e.c:237-238`) rather than
   dereferencing it; `rcp_e2e_unwrap()` falls back to a CRC-mismatch
   comparison without the copy if its `rcp_malloc()` fails
   (`src/e2e.c:316-323`); `rcp_watchdog_keeper_new()` returns `NULL`
   if its one `rcp_calloc()` fails (`src/watchdog.c:160-161`), the
   same already-existing failure channel every caller already checks.
   Mutation-testing the `e2e.c` sites (temporarily forcing `rcp_malloc`
   to return `NULL`) is already exercised by
   `tests/test_alloc_overflow.c`'s fault-injection harness — an
   allocator that returns `NULL` predictably (a well-behaved but
   resource-exhausted allocator) does not crash or corrupt state at any
   of these sites. A *misbehaving* hook that returns a dangling or
   undersized pointer instead of `NULL`, however, is not something any
   of these call sites can detect — that failure mode is outside what
   a freedom-from-interference argument at this layer can rule out; it
   would require the hook implementation itself to be trusted to the
   same ASIL, which is exactly this section's finding.

**Disposition:** this is added as a new binding Assumption of Use
(AoU-8, `SEOOC_BOUNDARY.md` §2) rather than asserted as resolved.
c-RCP cannot itself partition a single-process C allocator by ASIL —
that would require either a real memory-protection boundary (an
OS/hardware concern already covered by AoU-1) or a second, ASIL-rated-
only allocation path this library does not currently have. Introducing
one is a real, substantial design change (a new allocator indirection
layer, a compatibility-breaking API for any caller who already uses
`rcp_alloc_set_hooks()`) that this issue's own scoping — "each of these
is independently substantial; none should be attempted as a single-PR
change" — puts outside a documentation-focused pass. It is recorded
here, honestly, as the actual freedom-from-interference finding this
analysis produced, not papered over with the "QM/ASIL partition is
fine" conclusion a less careful pass might have reached.

## 3. `retired` catalog entries: no interference, by construction

The 6 `retired`-scope entries (`REQ-RMAP-004`..`008`, `REQ-RMAP-046`)
are dead requirement-catalog text kept only so `cfusa trace` does not
report a dangling reference for a `//cfusa:req` tag that still exists
somewhere in the source — per each entry's own text ("RETIRED as of
REQ-RMAP-030's primary-source verification milestone") none of them
describe code that still executes. `REQ-RMAP-004`..`008`'s tags *do*
still appear in `src/regmap.c:19-23`, exactly as expected (that
survival is the whole reason the catalog entries are kept rather than
deleted) — but immediately below them is a pure explanatory comment
(`src/regmap.c:24-36`, documenting why `rcp_regmap_options_group_consistent()`
was removed at v0.183.0), not a function body. The function these tags
originally described is confirmed gone:

```
$ grep -rn 'rcp_regmap_options_group_consistent' src/ include/
src/cli.c:135:   * when the now-removed rcp_regmap_options_group_consistent() enforced
src/regmap.c:25: * are pairwise distinct" / rcp_regmap_options_group_consistent()'s own
src/regmap.c:34: * described, and the rcp_regmap_options_group_consistent() function
include/rcp/regmap.h:394: * a now-removed rcp_regmap_options_group_consistent() function, citing
```

Every hit is a comment referencing the removal, not a declaration or
definition — there is no `rcp_regmap_options_group_consistent(...)  {`
anywhere in the tree. A requirement whose only surviving trace is a
`//cfusa:req` tag sitting above prose, with the function it once
described confirmed absent, cannot interfere with anything at runtime
— this is freedom-from-interference by construction, not by
mitigation, and needs no further argument.

`REQ-RMAP-046`, the sixth retired entry, is a different shape: its own
text records it as a stale *catalog duplicate* of `REQ-RMAP-039`
(issue #256 Group H), not a removed function — the register-map struct
fields it described are real, still-declared code, but they are
`REQ-RMAP-039`'s code, already accounted for under that (live,
`tc18`-scope) requirement's own freedom-from-interference posture in
§1's table. Retiring `REQ-RMAP-046` removed a duplicate description,
not a code path, so it adds no new interference surface beyond what
§1 already covers for `REQ-RMAP-039`.

## 4. `tc18-gap` entries: the text/scope-field drift this section flagged is now fixed (issue #548)

This section originally found (at the 136-entry HEAD this document was
first written against) that the `tc18-gap` scope's own catalog-note
invariant — QM by definition, never part of the ASIL-B safety-case
basis — did not actually hold: at least 49 entries' text began
"IMPLEMENTED" outright while `scope` was simply never moved back to
`tc18`, including all 10 of the scope's own ASIL-B-rated entries. That
finding was filed as issue #548 and closed by a dedicated
[c-RCP-16 follow-up] pass: every one of the (then-)136 `tc18-gap`
entries was independently re-verified against its actual
`//cfusa:req`/`//cfusa:test`-tagged code and tests (not trusted at the
text's own word), and 116 were confirmed genuinely complete and
reclassified to `scope: "tc18"` with a real ASIL rating derived from
sibling `tc18`-scope entries in the same functional area (mostly
ASIL-B; register-map informational/capacity fields and a handful of
narrative/config-plumbing entries correctly stayed QM even at `tc18`
scope, matching their siblings). A few PROMOTE-looking cases were
deliberately held back — e.g. `REQ-ISELED-028`, whose own text
self-describes as a stale duplicate of `REQ-ISELED-007` rather than a
closed implementation gap, so promoting it would have double-counted
`REQ-ISELED-007`'s ASIL-B coverage under a second id.

**19 entries remain genuinely `tc18-gap`** at HEAD, all QM (the
catalog note's invariant now actually holds, with zero exceptions):
`REQ-RMAP-023/043/044/045/065/067/081`, `REQ-ADC-053` (split 2026-08-18
from `REQ-ADC-037` by the [c-RCP-18-tracker] issue #533 `REQ-ADC-*`
atomicity batch -- the split separated `REQ-ADC-037`'s own now-fully-
tested `cadence_case()` contract, reclassified to `tc18`/ASIL-B, from
`rcp_ep_adc_cadence_response_ready()`'s own genuinely-partial
dispatch-wiring caveat, which `REQ-ADC-053` alone now carries),
`REQ-CANEP-029/030`, `REQ-DISC-029`, `REQ-GPIO-035`,
`REQ-LIFECYCLE-022/025/034`, `REQ-MDIO-024`, `REQ-PWM-057`,
`REQ-SPI-037`, `REQ-SRV-017`. Three of these
(`REQ-RMAP-081`/`REQ-SPI-037`/`REQ-CANEP-029`) still literally *begin*
"NOT IMPLEMENTED" and describe TC18 normative clauses this
implementation provides no code for at all — there is no function,
branch, or state write to analyze, the same "zero live footprint"
argument as §3. The rest are genuine partial implementations or
narrower open questions, each confirmed by direct code inspection
during the #548 pass to have a real remaining gap the entry's own text
(read in full, not just its leading word) still honestly describes.

`REQ-ISELED-028` (held back from promotion, above) no longer appears
in this list either — issue #552 moved it to `scope: "retired"`
instead, matching what its own text already said, rather than leaving
it as a `tc18-gap` entry that was never going to be closed by further
implementation work.

All 19 remaining `tc18-gap` entries, and the 116 now-`tc18`-scope
entries this section previously worried about, share one property that
*is* verifiable without a further per-entry audit: whatever their
ASIL rating, the live code they describe is subject to exactly the
same interference question as any other code in §1's table — not a
special "gap" category requiring separate treatment, and not exempt
from §2's shared-allocator-hook finding either. This document has
**not** individually audited every one of these entries' functions for
a direct write into ASIL-owned state beyond the general allocator-hook
argument in §2 and the #548 pass's own per-entry code verification
(which checked implementation completeness, not interference); a
dedicated per-function freedom-from-interference review remains real
remaining scope, not asserted as already covered.

## 5. Conclusion

Freedom-from-interference between c-RCP's QM-rated and ASIL-A/B-rated
requirement surface holds **by construction** for the `retired` (§3,
6 entries) and the 3 genuinely-not-implemented `tc18-gap` entries (§4)
— all have zero runtime footprint. It holds for the `tc18`-scope
QM-rated features and the 17 live-code `tc18-gap` entries **only
insofar as they do not call `rcp_alloc_set_hooks()`** — a real,
load-bearing dependency the two allocating ASIL-B safety mechanisms
(§2) share with every other caller in the process, with no partition
c-RCP can unilaterally enforce. That dependency is now recorded as
AoU-8 (`SEOOC_BOUNDARY.md`), not asserted as closed — consistent with
this issue's own instruction to document evidence rigor honestly
rather than claim a stronger posture than the code supports. §4's
`tc18-gap` text/scope-field drift this document originally surfaced
(49+ entries reporting "IMPLEMENTED" while still scoped as a gap,
including all 10 of the scope's own ASIL-B-rated entries despite the
scope's catalog note saying it should always be QM) was filed as issue
#548 and has since been fixed by a dedicated [c-RCP-16 follow-up] pass
— 116 entries reclassified to `scope: "tc18"` with a verified real
ASIL rating, 20 confirmed to have a genuine remaining gap and correctly
left `tc18-gap`/QM. The catalog-hygiene item is closed; this document's
own counts (§1, §4) reflect the corrected state.

---
_Document owner: SoundMatt/c-RCP maintainers_
_Review date: on next `.fusa-reqs.json` scope-field change, or annually,
whichever is sooner — same cadence as `SEOOC_BOUNDARY.md`_
_Standard: ISO 26262-6:2018 Clause 7 / ISO 26262-9:2018 Clause 6_
