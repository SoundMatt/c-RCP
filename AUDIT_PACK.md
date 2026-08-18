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
| Safety Requirements | `.fusa-reqs.json` | 1253 requirements; 1215 `scope: "tc18"` (1100 ASIL-B / 36 ASIL-A / 79 QM — this project's ISO 26262 safety-case basis), 23 `scope: "tc18-gap"` (normative-clause coverage markers, QM by definition), 9 `scope: "retired"`, 6 `scope: "internal"` — see the file's own `catalogNote` and `FREEDOM_FROM_INTERFERENCE.md` §1 for the current, verified breakdown (supersedes this table's own earlier 854/779/"legacy-compat" and 947/136 figures — the counts moved again via the [c-RCP-16 follow-up] issue #552 fix (`REQ-ISELED-028`: `tc18-gap` → `retired`), and the [c-RCP-18-tracker] issue #533 Group 3/Group 1/Group 2 (`REQ-ACF-*`, `REQ-AVTP-*`, `REQ-RMAP-*`, `REQ-SPI-*`, `REQ-ADC-*`, `REQ-GPIO-*`, `REQ-LINEP-*`, `REQ-ISELED-*`, `REQ-CANEP-*`, `REQ-UART-*`, `REQ-PWM-*`, `REQ-MDIO-*`) requirement-atomicity splits — the `REQ-ADC-*` batch's own `REQ-ADC-037`/`REQ-ADC-053` split additionally reclassified `REQ-ADC-037` from `tc18-gap` to `tc18`/ASIL-B once its atomicity split separated its now-fully-tested `cadence_case()` contract from `REQ-ADC-053`'s own genuinely-partial dispatch-wiring caveat, both after the issue #548 pass that reclassified 116 `tc18-gap` entries back to `scope: "tc18"` with a real ASIL rating, and the `REQ-CANEP-*`/`REQ-PWM-*` batches' own splits each added `tc18-gap` entries (`REQ-CANEP-038`; `REQ-PWM-068`/`-069`), moving that scope from 20 to 23; the `REQ-MDIO-*` batch's own split added no `tc18-gap` entries, leaving that scope unchanged at 23) |
| Safety Case | `safety-case.md` (auto-generated, `cfusa safety-case --gsn`) | CI gate |
| Release Badge | `fusa-badge.svg` (auto-generated, `cfusa badge`) | CI gate |
| SEOOC Boundary & Assumptions of Use | `SEOOC_BOUNDARY.md` | Added c-RCP-16 (issue #518) — see §2a |
| Freedom-from-Interference Argument | `FREEDOM_FROM_INTERFERENCE.md` | Added c-RCP-16 item 4 (issue #518) — QM/ASIL-A/B co-existence analysis for the single-binary partition above |
| Tool Qualification Evidence | `qualify-report.json` (auto-generated, `cfusa qualify`) | CI gate — self-qualified, see §2a |
| MC/DC Coverage (informational) | `mcdc-summary.json`/`mcdc-export.json` (CI artifact, `ci.yml`'s `mcdc` job) | Added c-RCP-16 item 3 (issue #518) — genuine LLVM condition/decision coverage, non-gating; see §3 |

---

## 2. ASIL-C Gap Analysis / SEOOC Evidence Posture (ISO 26262 §7)

Framing note (c-RCP-16, issue #518 item 5): this section was originally
written, and is kept below largely as originally written, as an
**internal** derogation table — this project's own record of why it
has not pursued certain ASIL-D-tier rigor items on top of its actual
ASIL-B/C baseline. Read on its own, that internal framing risks
under-selling what the table actually is to a different audience: for
an **integrator** performing their own item-level HARA (ISO
26262-3:2018 Clause 6) over a vehicle function that uses c-RCP, this
same table is real evidence toward — without asserting — an ASIL-D
item-level safety case, because it inventories exactly the rigor items
(redundant delivery paths, formal deadlock-absence proofs, MC/DC, MISRA
mandatory/required compliance) ISO 26262-10:2018 Clause 9's SEOOC
evidence expectations ask a supplied element to have reasoned about,
whether or not this project's own baseline currently obligates them.
`SEOOC_BOUNDARY.md` is the dedicated integrator-facing document built
for that audience (§2a below); this section remains the primary-source
detail underneath it, cross-referenced rather than duplicated.

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
| 100% MC/DC structural coverage | Branch coverage is captured (`lcov --rc branch_coverage=1`, both `ci.yml`'s `coverage` job and `release.yml`); real MC/DC is now measured (informationally, non-gating) by `ci.yml`'s `mcdc` job (c-RCP-16 item 3, issue #518) | See §3 |

Unlike the ASIL-D-requirement rows above (a deliberate, reasoned choice
to not pursue a higher rigor level for an already-implemented
mechanism), H-004 and H-007's "Not implemented in this library" rows
are genuinely open gaps this re-certification pass surfaced rather than
closed — recorded honestly here rather than folded into the same
"derogation" framing as the others, which would misrepresent an absent
control as a considered rigor trade-off.

### Dynamic Allocation Posture (issue #521)

MISRA C:2012 Rule 21.3 ("The memory allocation and deallocation
functions of `<stdlib.h>` shall not be used") and the equivalent ISO
26262-6 guidance against dynamic allocation on ASIL-D software are
**advisory-tier** items this project's own baseline (ASIL-B, one
hazard at ASIL-C) does not obligate — see the MISRA row above. Issue
#521 tracks moving *toward* the stricter posture anyway, as one of this
project's own stated stretch items. Progress so far, in the same
phased order the issue itself defines:

- **Phase (a) — abstraction-bypass gap, CLOSED.** Every dynamic
  allocation call site in `src/` (256 of them, up from the 8 that
  already used it) routes through `alloc.h`'s `rcp_malloc`/`rcp_calloc`/
  `rcp_realloc`/`rcp_free` seam. Every remaining site described below is
  therefore already interceptable/boundable by an integrator's own
  monitoring or fault-injection hook, regardless of which category it
  falls into.
- **Phase (b) — convert boundable cases to fixed-capacity storage,
  CLOSED.** Converted across rounds 1–4: `l2.c`/`udp.c` receive
  buffers, `watchdog.c`/`deadline.c` stream tables and callback lists,
  `powerstate.c`/`admin.c` endpoint/subscriber/counter tables,
  `respqueue.c`'s `entries[]`/`entries_seq[]` free/FIFO-order
  bookkeeping (`RCP_RESPQUEUE_MAX_ENTRIES`, now a universal bound, not
  only a `capacity_octets == 0` fallback), `loan.c`'s pool free-list
  bookkeeping (`RCP_LOAN_POOL_MAX_ENTRIES`), `ep_can.c`'s
  prefix-then-data scratch buffer and fragment-plan segment array
  (`RCP_EP_CAN_MAX_FRAGMENT_SEGMENTS`), and — round 4, closing this
  phase out — `discovery.c`'s own fragment-plan segment array
  (`RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS`, 255). Every one of these was
  a case where the array's true worst-case size was already a small,
  real compile-time protocol constant, so a fixed embedded array is a
  behavior-preserving (mutation-tested) drop-in.

  **Why CLOSED, not merely "no further items found this round" again:**
  round 3's own status comment made exactly that claim ("No further
  'convert this to fixed-capacity' work remains identified") without
  a fresh, exhaustive re-enumeration of every current call site behind
  it — round 4 did that re-enumeration (every `rcp_malloc`/`rcp_calloc`/
  `rcp_realloc(` call site in `src/*.c`, grepped fresh rather than
  trusted from a prior count) and it found exactly one real remaining
  oversight: `discovery.c`'s fragment-plan array, converted above. The
  file-by-file classification in Phase (c) below is that same
  enumeration's complete result, not a curated illustrative sample —
  see its own opening paragraph for the reconciliation that makes this
  checkable rather than asserted.

  **The one call site that turned out to be real-constant-bounded but
  is deliberately NOT converted** (`request_sequencer.c`'s
  `rcp_sequencer_table_new(uint16_t count)`, see Phase (c) category 4
  below) is not a Phase (b) gap: converting it would require changing
  `rcp_sequencer_table_t.state`/`.owner` from heap pointers to embedded
  fixed arrays, which breaks that struct's own documented and
  test-pinned "NULL means `count == 0` / allocation failure" public API
  contract (`tests/test_request_sequencer.c` asserts
  `TEST_ASSERT_NULL(table.state)` six times; `mock.c` checks
  `srv->sequencers.state != NULL` directly) — exactly the "different,
  breaking API contract Phase (b)'s mechanical, behavior-preserving
  scope does not license" reasoning already established for `loan.c`
  and `relay.c` below, not a case Phase (b) left on the table by
  oversight.
- **Phase (c) — document/justify what remains genuinely dynamic.**
  Every dynamic-allocation call site remaining in `src/*.c` after
  Phase (b) — **90 real call sites across 39 files**, as of this PR —
  falls into exactly one of the six shapes below. This count is
  mechanically reproducible and was re-verified against the actual
  current tree for this pass, not carried forward from an earlier
  round's number:

  ```
  grep -nE '\brcp_(malloc|calloc|realloc)\s*\(' src/*.c | grep -v '/alloc\.c:'
  ```

  returns 95 lines (`alloc.c` itself is excluded: it only *defines*
  `rcp_malloc`/`rcp_calloc`/`rcp_realloc`, and a naive grep matches
  those three definition lines too). Of those 95, five are source
  comments that merely mention the function names in prose rather than
  call them (`admin.c:44`, `admin.c:153`, `l2.c:125`, `udp.c:126`,
  `request_sequencer.c:9` — each citable by line number and readable in
  context) and are not call sites at all. **95 − 5 = 90**, the number
  the categories below sum to exactly. `config.c`'s one grep hit is a
  three-way `#define DEFINE_APPEND(...)` macro body (`append_pin`/
  `append_endpoint`/`append_stream`), counted once here as one textual
  call site, matching how the grep above counts it.

  | # | Shape | Sites | Files |
  |---|---|---|---|
  | 1 | `rcp_bytes_t`/`relay_bytes_t` owned output buffer, sized to that call's own input | 34 | 19 |
  | 2 | Fragment-plan segment array, no small compile-time bound | 2 | 2 (subset of row 1's files) |
  | 3 | `loan.c` pooled buffer data + per-acquire control blocks | 4 | 1 |
  | 4 | Deployment/config-scale table or capacity, no protocol-defined bound | 22 | 12 |
  | 5 | One-time, setup-path allocation | 6 | 2 |
  | 6 | Opaque-handle/object constructor (`_new()`'s own `sizeof(*handle)`) | 22 | 18 (+ `loan.c`, tallied under row 3) |
  | | **Total** | **90** | **39 distinct files** |

  The 39-file union across all six rows is exactly: `acf.c`, `adapt.c`,
  `admin.c`, `authz.c`, `avtp.c`, `config.c`, `deadline.c`,
  `discovery.c`, `e2e.c`, `ep_adc.c`, `ep_gpio.c`, `ep_i2c.c`,
  `ep_iseled.c`, `ep_lin.c`, `ep_mdio.c`, `ep_pwm.c`, `ep_spi.c`,
  `ep_uart.c`, `ep_wakeup.c`, `faultinject.c`, `fragment.c`, `l2.c`,
  `loan.c`, `mdns.c`, `mock.c`, `observe.c`, `platform.c`,
  `powerstate.c`, `ratelimit.c`, `rcp.c`, `recorder.c`, `relay.c`,
  `request.c`, `request_sequencer.c`, `server.c`, `shmem.c`, `tsn.c`,
  `udp.c`, `watchdog.c` — the same 39 files `grep -l` returns against
  the pattern above (minus `alloc.c`). `ep_can.c` and `respqueue.c`
  appear in neither list: both were fully converted (Phase (b)) and
  carry zero dynamic allocation of their own today.

  1. **Variable-length owned output buffers (`rcp_bytes_t`/
     `relay_bytes_t`).** One `malloc()` per call, sized to that
     specific call's own input (a payload, a data buffer, a symbol
     count, a frame length) and freed by the caller — this codebase's
     single foundational "variable-length owned buffer" convention,
     used by essentially every public encode/decode API and by
     `rcp_bytes_dup()`/`relay_bytes_dup()` themselves (`rcp.c`,
     `relay.c`), not a small fixed protocol maximum in every case
     (e.g. `rcp_ep_iseled_encode_bitframe()`/`_decode_bitframe()`,
     `include/rcp/ep_iseled.h`, scale directly with a caller-supplied
     `data_len`/`symbol_count` with no small compile-time ceiling of
     their own — ISELED chain length is a deployment property, not a
     TC18 wire constant). Replacing this convention everywhere would
     mean redesigning that entire public surface to a
     caller-supplied-buffer-and-capacity shape, a breaking API change
     far outside this issue's own "convert boundable cases" phase (b)
     scope. `fragment.c`'s reassembly buffer (grows to a
     caller-configured `max_total_len`, `include/rcp/fragment.h`'s own
     file header) is the same shape: the reassembled message's true
     size is a deployment-configured ceiling, not a protocol constant.
     Files (34 sites / 19 files): `acf.c` (2), `avtp.c` (2), `e2e.c`
     (2), `ep_adc.c` (2), `ep_gpio.c` (1), `ep_i2c.c` (1), `ep_iseled.c`
     (3), `ep_lin.c` (1), `ep_mdio.c` (5), `ep_pwm.c` (2), `ep_spi.c`
     (1), `ep_uart.c` (1), `ep_wakeup.c` (1), `fragment.c` (1), `l2.c`
     (1), `rcp.c` (1, the `rcp_bytes_dup()` definition itself), `relay.c`
     (1, `relay_bytes_dup()`), `request.c` (5), `udp.c` (1).
  2. **Fragment-plan segment arrays with no small compile-time
     bound.** `ep_iseled.c`'s (`rcp_ep_iseled_encode_response_
     fragmented()`) and `ep_uart.c`'s (`rcp_ep_uart_encode_read_
     response_fragmented()`) own internal `rcp_fragment_segment_t`
     arrays are deliberately NOT converted to fixed arrays the way
     `ep_can.c`'s and (round 4) `discovery.c`'s equivalents were: both
     plan against a length with no small type-derived ceiling —
     ISELED's is bounded only by `read_size` (a full 16-bit register,
     up to 65535) and the caller's own `rx_len`; UART's `rx_len` is a
     plain `size_t` with no narrower parameter type at all, and
     `ep_uart.h`'s own file comment (REQ-UART-034, issue #201) records
     that a conforming UART read response can genuinely carry up to
     4095 octets, not merely a handful — a fragment count in the
     thousands is possible via `fragment.h`'s own global
     `RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS` (4096) ceiling, but an
     array that size (well over 90KB at `sizeof(rcp_fragment_segment_t)`
     per entry) is not safe to put on a call stack. This is genuinely
     different from `discovery.c`'s case (round 4, now converted, Phase
     (b) above): `discovery.c`'s `read_size` parameter is actually typed
     `uint8_t`, a hard compile-time guarantee its payload never exceeds
     255 octets regardless of `max_fragment_payload` — `ep_uart.h`'s own
     file header draws an analogy between the two endpoints'
     real-world traffic patterns ("like ep_uart.h's read responses,
     never actually needs fragment.h's ms/segment_num mechanism... in
     real-world use"), but that analogy is about typical traffic, not
     about the two functions' actual worst-case bounds, which differ —
     see `RCP_DISCOVERY_MAX_FRAGMENT_SEGMENTS`'s own doc comment
     (`discovery.h`) for the distinction spelled out in full. Files (2
     sites / 2 files, both already counted under row 1 above for their
     own separate encode-buffer sites): `ep_iseled.c`, `ep_uart.c`.
  3. **`loan.c`'s pooled buffers and per-acquire control blocks.** The
     free-list bookkeeping array is fixed-capacity (Phase (b)), but
     each pooled buffer's own DATA bytes, the pool object itself
     (`rcp_loan_pool_new()`), and the small `rcp_loan_t`/
     `loan_release_ctx_t` control structs `rcp_loan_pool_acquire()`
     allocates per call, remain heap-based — the module's entire
     documented purpose (`loan.h`'s own file header) is caching reuse
     of buffers whose size is a caller-chosen, runtime `size` argument,
     not a compile-time constant; eliminating that would mean
     redesigning the pool into fixed-size-slab semantics (reject any
     `size` above one fixed slot size), a different, breaking API
     contract this issue's phase (b) — mechanical, behavior-preserving
     conversion — does not license. Files (4 sites / 1 file): `loan.c`.
  4. **Deployment/config-scale tables and capacities, no
     protocol-defined bound.** A growable table (realloc-doubling, the
     same `entries_len == entries_cap` pattern throughout) or a single
     allocation sized once to a caller-chosen "how many"/"how deep"
     parameter at construction time, in every case for a quantity TC18
     itself gives no bound for because the thing being counted is not a
     TC18 wire structure at all — an access-control rule, a discovered
     peer, a rate-limited address, a fault-injection rule, a captured
     trace frame, a pending request queued for a disabled endpoint, a
     RELAY metadata entry or channel's queue depth, a manifest-declared
     hardware pin/endpoint/stream, a shared-memory or loopback
     transport's own queue capacity. This is `relay.c`'s
     `relay_meta_entry_t` key/value table and its message channel's
     `items[]` queue (channel depth is this library's own
     `relay_subscriber_options_t.channel_depth`/`relay_message_channel_
     new(capacity)` design, not a TC18 field) generalized to every
     structurally identical table elsewhere: `authz.c`'s policy
     `entries[]` (an ACL, not wire state), `discovery.c`'s client-side
     result `cache` (how many servers a client has seen, not a
     register), `ratelimit.c`'s per-address `buckets[]` (grows per
     distinct peer address actually seen — closer to a hot path than
     the others in this row, but still bounded only by how many
     distinct addresses a deployment's traffic contains, not a protocol
     constant), `observe.c`'s in-memory span log and `recorder.c`'s
     trace-capture log (both diagnostic/tooling sinks whose whole
     purpose is recording everything that happens), `faultinject.c`'s
     rule table, `server.c`'s per-endpoint pending-request queue,
     `config.c`'s build-time manifest scan (`hw_pin_map`/`endpoints`/
     `streams`, a local integration file, not wire traffic), `shmem.c`'s
     and `avtp.c`'s (loopback transport) own queue-capacity buffers.
     `request_sequencer.c`'s `rcp_sequencer_table_new(uint16_t count)`
     is a documented exception worth naming precisely rather than
     folding in silently: unlike every other file in this row, `count`
     genuinely IS bounded by a real protocol constant in legitimate use
     — `regmap.h`'s `svr_sequencers_max` (REQ-RMAP-028) is an 8-bit wire
     field (0..255), and `request_sequencer.h`'s own file header
     documents that a table is always "sized at runtime from a server's
     own `svr_sequencers_max` register value" (a sibling constant,
     `RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES` = 0xFF, already exists and
     is used for a *different* code path's own stack-local copy,
     `regmap.h`'s own comment there). What keeps this a Phase (c) case
     rather than a Phase (b) conversion is not the bound (it exists) but
     that `rcp_sequencer_table_t.state`/`.owner` are public, tested
     heap-pointer fields with a documented "NULL means unallocated"
     sentinel contract (six `TEST_ASSERT_NULL(table.state)` assertions
     in `tests/test_request_sequencer.c`, a direct `!= NULL` check in
     `mock.c`) that embedding fixed arrays in their place would break —
     see the Phase (b) bullet above for this same point stated from the
     "why this isn't a Phase (b) gap" side. Files (22 sites / 12 files):
     `relay.c` (6), `authz.c` (3), `shmem.c` (2), `recorder.c` (2),
     `request_sequencer.c` (2), `discovery.c` (1), `ratelimit.c` (1),
     `observe.c` (1), `faultinject.c` (1), `server.c` (1), `config.c`
     (1), `avtp.c` (1).
  5. **One-time, setup-path allocations.** `mdns.c`'s config-time
     string duplication (`dup_cstr()`) and static discovery-record
     table (sized once, at construction, to a caller-supplied `count` —
     never regrown), and `platform.c`'s thread-thunk allocation (one
     tiny fixed-size struct per `rcp_thread_start()`/
     `_start_detached()` call, freed by the thread trampoline itself
     before the user function runs) — both are service/hostname/
     thread-argument allocations whose frequency is deployment
     configuration or thread-launch count, not a per-message hot path.
     This is the same "one-time control block, not a repeated
     per-message allocation" shape ISO 26262-6's dynamic-allocation
     concern (mission-length heap fragmentation from repeated
     alloc/free cycles) is centrally about, and is a materially
     different risk profile than the per-message paths Phase (b)
     targeted first. Files (6 sites / 2 files): `mdns.c` (2, plus one
     more site — its own discoverer object constructor — tallied under
     row 6), `platform.c` (4).
  6. **Opaque-handle/object constructors.** The C opaque-pointer
     object-lifecycle idiom this codebase uses for nearly every
     stateful module: `X_new()`/`_create()`/`_avtp_pair_new()` allocates
     its own top-level instance struct exactly once, sized to a
     compile-time-constant `sizeof(*handle)` (never proportional to any
     message or hot-path input), and the matching `_free()`/`_destroy()`/
     `_release()` frees it exactly once. `admin.c`, `deadline.c`,
     `powerstate.c`, and `watchdog.c` each look, from their single
     remaining `rcp_calloc(1, sizeof(*x))` call, like they might still
     have unconverted state — they do not: Phase (b) already made every
     *internal* table in each of these four structs (endpoints/
     subscribers/counters, stream tables, callback lists) a fixed
     embedded array; the one call site left in each file is this
     row's shape, the struct's own handle allocation, not a leftover
     internal array. Files (22 sites / 18 files, `loan.c`'s own pool
     constructor is the same shape but already tallied under row 3 to
     avoid double-counting): `shmem.c` (3), `l2.c` (2), `udp.c` (2),
     `admin.c`, `adapt.c`, `authz.c`, `avtp.c` (the loopback transport
     constructor — separate from row 1's two encode-buffer sites in the
     same file), `deadline.c`, `faultinject.c`, `mdns.c` (its
     discoverer object, distinct from row 5's two sites in the same
     file), `mock.c`, `observe.c`, `powerstate.c`, `ratelimit.c`,
     `recorder.c`, `relay.c` (its message-channel struct — separate
     from row 4's six metadata/queue-items sites in the same file),
     `tsn.c`, `watchdog.c`.

  **Reconciliation:** 34 + 2 + 4 + 22 + 6 + 22 = **90**, matching the
  grep-derived total above exactly, across the same 39 files listed
  above — every number in this section is mechanically checkable
  against the current tree, not asserted. None of these six shapes is
  a case this issue's own phase (b) scope ("convert boundable cases")
  actually covers — each is either unbounded by any small real
  constant, bounded but not convertible without a breaking public API
  change (row 4's `request_sequencer.c` exception), or would require a
  breaking public API redesign more generally (rows 1 and 3). All of
  them are already on the `alloc.h` seam (Phase (a)), so an integrator
  wanting stricter, ASIL-D-tier control over any of them today can
  install `rcp_malloc`/`rcp_calloc`/`rcp_realloc` hooks that route to a
  static/pool allocator of their own choosing without this library
  changing at all.

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

**MC/DC (Modified Condition/Decision Coverage), added c-RCP-16 item 3
(issue #518).** `ci.yml`'s `mcdc` job now measures real, genuine MC/DC
via LLVM's own source-based coverage instrumentation
(`clang -fcoverage-mcdc`), distinct from the branch-coverage proxy
`cfusa coverage --dal DAL-A`/`--asil ASIL-D` would otherwise silently
substitute (`cfusa coverage --help`'s own words: "NOT verified MC/DC
evidence"). It does **not** route through `cfusa coverage
--mcdc-file` — that flag's parser expects literal
`"covered_true_count"`/`"covered_false_count"` JSON keys that real
`llvm-cov export` never emits at any LLVM version (confirmed against
upstream LLVM's `CoverageExporterJson.cpp`, which emits positional
arrays instead — filed and tracked as SoundMatt/c-FuSa#129). The `mcdc`
job reads `llvm-cov export`'s own `totals.mcdc` block directly instead,
so the number is genuine MC/DC evidence today, not blocked on that
upstream fix. Verified end-to-end locally (Apple Clang 21, same flags/mechanism the
`ci.yml` job uses with `clang-18` on Ubuntu — the exact percentage is
expected to vary marginally by LLVM version and is CI's own
`mcdc-summary.json` artifact's job to report on each run, not this
document's) against the current 67-test suite (`src/*.c` only,
matching the `coverage` job's own exclusion of
`tests/`/`unity/`/`_deps/`): **64.4% MC/DC condition-pair coverage
(437/679)**, against 83.7% branch coverage over the same instrumented
binaries — the two numbers diverging by ~19 points is itself the
concrete demonstration of why a branch-coverage proxy is not a
substitute for real MC/DC evidence at ASIL-C/D (ISO 26262-6:2018
Table 12). Per this issue's own suggested sequencing, the job is
**informational only** — no step in it fails the build; whether/when
to introduce a hard MC/DC threshold is deliberately left as a future
decision, matching how `cfusa trace --req-coverage` was rolled out
informationally before becoming a hard gate between v0.2.0 and v0.53.0.

**Platform-conditional carve-out (issue #520 category 3).** `ci.yml`'s
`coverage` job runs on `ubuntu-22.04` only. Three first-party files
carry a `#if defined(_WIN32)` block that is real code compiled and
exercised by the hard-gated `windows-2022 / msvc` `build-and-test`
matrix leg on every PR, but it is structurally unreachable by the
Linux runner that produces `coverage.info`, no matter how much
test-writing effort targets it:

- `src/platform.c`'s Win32 mutex/condvar/thread wrappers
  (`InitializeCriticalSection`/`CreateThread`/`SleepConditionVariableCS`
  and siblings) -- a full, working implementation, not a stub
- `src/clock.c`'s Win32 monotonic/wall-clock read path
  (`QueryPerformanceCounter`/`GetSystemTimeAsFileTime`) -- likewise a
  full working implementation
- `src/udp.c`'s `#else /* !RCP_UDP_POSIX */` branch -- by contrast, a
  deliberate fail-closed *stub* ("no winsock implementation yet", per
  its own comment; `ROADMAP.md` tracks the real implementation as
  future work), every entry point returning `RCP_ERR_CLOSED`/`RCP_OK`
  without touching a socket. Structurally unreachable from the Linux
  `coverage` job for the same reason as the other two, but note the
  underlying gap here is a missing feature, not missing tests.

This is a permanent, reasoned carve-out, not a backlog item: the fix
would be running `coverage` on `windows-2022` too and merging both
runners' `coverage.info` files, a CI-topology change out of scope for
a documentation note, not a test-writing gap in these three files
themselves. `src/l2.c`'s `__linux__`-conditional `AF_PACKET` path is
the mirror image — it *is* exercised for real, by the separate
`l2-transport-veth` job's actual root-privileged veth round trip
(`CAP_NET_RAW`, outside `coverage`'s own unprivileged `ctest`
invocation) — so its number inside `coverage.info` under-reports real
exercise rather than reflecting a genuine gap, and is not comparable
to the three `_WIN32` files above on that basis.

---

## 4. DO-178C (DAL-B) Applicability

If c-RCP is used in an airborne system under DO-178C DAL-B:

- Source code traceability to LLR: via `//cfusa:req` annotations —
  `.fusa-reqs.json`'s `scope: "tc18"` subset is this project's actual
  LLR basis; `scope: "tc18-gap"`/`"retired"`/`"internal"` entries are
  informational only (see §1)
- Tool qualification: `cfusa` is a Tool Qualification Level analysis
  tool — see `qualify-report.json`
- Decision coverage: MC/DC required at DAL-B — real, non-gating MC/DC
  measurement now exists (§3's `mcdc` job); no hard threshold yet, see
  §3 for the current measured percentage
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
(1253 requirements at HEAD: 1215 `scope: "tc18"` covering the
register-map, lifecycle FSM, E2E safe points, every endpoint type's
request/response shape, discovery, power-mode transitions, and every
ADAPT-class satellite; 23 `scope: "tc18-gap"` marking TC18 normative
clauses this implementation does or doesn't fully meet, QM by
definition; 9 `scope: "retired"` — dead requirement text kept only
because a surviving `//cfusa:req` tag still names the ID, see
`FREEDOM_FROM_INTERFERENCE.md` §3; 6 `scope: "internal"` — the
allocator-hook indirection layer. This table's earlier 854/779/75
"legacy-compat" figures described a pre-v0.91.0 codebase state — the
pre-TC18 Zone/Command surface and `tests/legacy_mock.*` they referred
to were fully removed at v0.91.0 per `CHANGELOG.md`'s Deprecation &
Removal Log; the subsequent 947/136 figures predate the
[c-RCP-16 follow-up] issue #548 pass, which independently re-verified
every `tc18-gap` entry against its actual `//cfusa:req`/`//cfusa:test`
code and reclassified 116 of them (a stale `scope` left over from
closing the gap they described) to `scope: "tc18"` with a real ASIL
rating; see `FREEDOM_FROM_INTERFERENCE.md` §1/§4 for the current
verified breakdown). `cfusa trace
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
   set `scope: "tc18"` for anything describing shipped, fully-conformant
   TC18 behavior; `scope: "tc18-gap"` for a normative clause not yet
   (or only partially) met, with the entry's own text kept current as
   the implementation changes (see `FREEDOM_FROM_INTERFERENCE.md` §4
   for what happens when it isn't); `scope: "retired"`/`"internal"` are
   not intended to gain new entries outside their existing narrow use
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
