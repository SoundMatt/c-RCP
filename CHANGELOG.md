# Changelog

All notable changes to c-RCP are recorded here, one entry per tagged
release, newest first. Generated retroactively (issue #108) from
`ROADMAP.md`'s existing per-milestone detail and this repository's own
tag history; maintained going forward as a release-gate requirement
alongside `ROADMAP.md`, per RELAY spec §19.2.

## Deprecation & Removal Log

Per RELAY spec §19.2, this table records every module deprecation:
the version it was deprecated in, its replacement (if any), and the
version it was actually removed in. Both entries below deprecated and
removed a module in the *same* release, with no intervening MINOR
deprecation window -- see each module's own release entry below for
the rationale.

| Module | Deprecated in | Replacement | Removed in |
|---|---|---|---|
| `tls.h`/`tls.c` | v0.78.0 | MACsec (802.1AE, link-layer; out of this library's scope) | v0.78.0 |
| `prioqueue.h`/`prioqueue.c` | v0.83.0 | Protocol-defined request-kind execution priority (server-side) | v0.83.0 |
| `firmware.h`/`firmware.c` | v0.83.0 | None (OTA is explicitly an OEM/application-layer concern, out of RCP's scope) | v0.83.0 |
| `zonegroup.h`/`zonegroup.c` | v0.83.0 | None (no multi-target broadcast grouping concept in TC18) | v0.83.0 |
| `proxy.h`/`proxy.c` | v0.83.0 | None (multi-hop/bridging is a network concern, not RCP's) | v0.83.0 |
| `redundancy.h`/`redundancy.c` | v0.83.0 | None (no server-redundancy concept in TC18) | v0.83.0 |
| `federation.h`/`federation.c` | v0.83.0 | None (no TC18 counterpart to the zone/HPC-lease registry model) | v0.83.0 |
| `dyndata.h`/`dyndata.c` | v0.83.0 | None (every TC18 endpoint has one fixed, spec-defined payload shape) | v0.83.0 |
| `wire.h`/`wire.c` (the `'RC'`-magic placeholder codec) | v0.91.0 | The real TC18 AVTP/ACF layer (`avtp.h`/`avtp.c`, `acf.h`/`acf.c`), present and primary since Phase 13 | v0.91.0 |
| `sim.h`/`sim.c` (zone-controller simulator) | v0.91.0 | None in this repo; SiL/HIL simulation against the TC18 RC Server/Endpoint model can be built on `mock.h`/`mock.c` instead | v0.91.0 |
| `rcp.h`/`rcp.c`'s pre-TC18 object model (`rcp_zone_t`, `rcp_command_t`, `rcp_response_t`, `rcp_status_t`, `rcp_controller_t`, `rcp_registry_t`) | v0.91.0 | The TC18 register-map/lifecycle/endpoint core (`regmap.h`, `lifecycle.h`, `ep_*.h`) | v0.91.0 |
| `ep_lin.h`/`ep_lin.c`'s `rcp_ep_lin_compare_mode_t`/`rcp_ep_lin_compare_fires()` (an invented eight-value evt[2:0] comparison scheme, self-admittedly not spec-derived) | v0.112.0 | `rcp_acf_evt_row2_is_plain()` validation + `rcp_ep_lin_response_matches()` (delegates to acf.h's shared TC18 §13.5.1 exact-match primitive) | v0.112.0 |
| `regmap.h`/`regmap.c`'s `rcp_regmap_options_group_consistent()` and its six paired `RCP_REGMAP_OPT_*` constants (an invented all-or-nothing-pair grouping for `svr_implemented_options`, citing a section that, on primary-source verification, does not describe it) | v0.183.0 | Five independent single-bit `RCP_REGMAP_OPT_*` constants matching TC18 §12.7.5 Table 18 exactly | v0.183.0 |
| `acf.h`'s `RCP_ACF_ERR_BUS_ID_OVERFLOW` (`rcp_acf_errc_t`'s last value; signaled a decoded `byte_bus_id[10:8]` the 8-bit-wide `rcp_byte_bus_id_t` couldn't represent) | v0.197.0 | None -- `rcp_byte_bus_id_t` (`avtp.h`) is now `uint16_t`, wide enough for the full 11-bit wire field, so the condition this code represented can no longer occur | v0.197.0 |

## Releases

### v0.308.0 -- 2026-08-13 (`REQ-SEQ-013`/`REQ-SEQ-014` citation-drift correction, issue #341 lineage)

**Doc-only.** Fixes a real citation bug this project's own most recent batch (v0.306.0) introduced, discovered while working the immediately following batch (`REQ-MDIO-022`/`REQ-MDIO-024`, v0.307.0). This repo's own cached primary-source reference files (`/Users/matt/Documents/Coding/SoundMatt/TC18.txt`/`TC18_full.txt`/`TC18_nopgbrk.txt`, canonical path outside this repo) were discovered to be stale RC1-dated `pdftotext` dumps despite this project's own prior "TC18 spec rebaseline to 0.5.1_RC5" work -- the real RC5 PDF was apparently never re-extracted to `.txt`. Content for the MDIO section happened to be unchanged between RC1/RC5 (only table numbers and the page date differed), but the sequencer section's own line numbers shifted by roughly 400 lines. `REQ-SEQ-013`/`REQ-SEQ-014`'s v0.306.0 citations (`TC18.txt L3064-L3110`) were verified against that stale file at the time and were internally consistent with its own (RC1) content -- but wrong against the file once correctly refreshed to RC5. The correct citation, confirmed against the freshly re-extracted RC5 text, is `TC18.txt L3460-L3493` -- which also happens to match `REQ-SEQ-012`'s own pre-existing citation exactly (that citation was never actually wrong; an earlier working note in this same investigation had wrongly flagged it as a bug based on the same stale-file comparison, now retracted).

The three cached reference files have been re-extracted fresh from the real RC5 PDF (`OA_TC18_specification_v_0.5.1_RC_5_3624.pdf`) as part of this fix, so this specific staleness class cannot recur for future citation work in this repo. No code changed -- `.fusa-reqs.json` citation text only. `cfusa check`/`trace` re-run to confirm: 0 errors, 1024/1024 traced and tested, identical to the pre-change baseline.

### v0.307.0 -- 2026-08-13 (`REQ-MDIO-022`/`REQ-MDIO-024`: MMS addressing and 32-bit data fields, informed by a real external spec)

Closes `REQ-MDIO-022` (TC18 §13.7.13.3 Table 60's 32-bit-for-MMS0/MMS1 data-field rule) and `REQ-MDIO-021`'s own remaining PARTIAL scope, and adds a new `REQ-MDIO-024` for the assumption this rests on -- the first fix in this project to work around a genuine TC18 spec silence using a real, external, publicly-available specification rather than either an invented-from-nothing assumption or leaving the gap open.

- **Investigated before touching code**: re-verified `REQ-MDIO-022`'s "blocked" status directly against a freshly `pdftotext`-extracted copy of the actual RC5 PDF (not the repo's own stale RC1-dated `.txt` renderings, discovered to be stale in the process -- worth a follow-up citation-drift pass under issue #341, not done here) before accepting the prior investigation's conclusion. Confirmed: TC18 states the 32-vs-16-bit rule directly (not an assumption), but never gives `mdio_address` a bit width for MMS mode, matching `TC18_spec_defects_report.md` item 55 exactly.
- **Found the real external spec**: a web search located the actual OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface specification, V1.1 (public, `opensig.org`) -- stored at `/Users/matt/Documents/Coding/SoundMatt/OPEN_Alliance_10BASE-T1x_MAC-PHY_Serial_Interface_V1.1.pdf`. Its own control command header (§7.4.1 Table 4) shows the real shape "MMS" terminology is almost certainly borrowed from: a 4-bit MMS selector (0-15, §9.1 Table 6) followed by a 16-bit ADDR field. Its own Table 6 also independently confirms TC18's 32-vs-16-bit split for MMS0/2-6 (MMS1 is marked "implementation dependent" in the real spec -- TC18's own 32-bit-for-MMS1 rule is an RCP-specific overriding convention, not simply quoted).
- **`rcp_ep_mdio_mms_addr_t`** (new, `ep_mdio.h`/`.c`): assumes TC18's own `mdio_address` packs the same 4-bit-MMS + 16-bit-ADDR shape, represented on this module's own wire as two whole octets (matching the existing MMD prefix's own "give every sub-field its own octet" convention). Catalogued as `REQ-MDIO-024`, status `PARTIAL` (not `IMPLEMENTED`): the address-layout assumption could be wrong if the real TC18 committee resolution of item 55 differs -- the 32-vs-16-bit width rule itself, by contrast, is TC18-literal and closes cleanly.
- **New parallel `*_mms_*` function family**, purely additive -- the existing MMD family (`rcp_ep_mdio_addr_t`, every pre-existing read/write encode/decode function) is unchanged byte-for-byte: `rcp_ep_mdio_word32_encode()`/`_decode()`, `rcp_ep_mdio_mms_pack_words()`/`_word_count_of()`/`_unpack_word_at()` (uint32_t-typed at the API boundary, width selected per `rcp_ep_mdio_mms_uses_32bit_words()`), and the full `encode_mms_read_request()`/`decode_mms_read_request()`/`encode_mms_read_response()`/`decode_mms_read_response()` + write equivalents.
- **Symmetric mode-routing errors**: `RCP_EP_MDIO_ERR_UNSUPPORTED_MMS` keeps its name (source compatibility) but now means "wrong decoder family, use `*_mms_*` instead" rather than "MMS is unsupported"; the new `RCP_EP_MDIO_ERR_WRONG_MDIO_MODE` is its mirror image, returned by the `*_mms_*` decoders for an MMD-mode frame. Mutation testing initially caught only the read-side mirror test missing for the write side -- added `test_mms_write_request_decode_rejects_mmd_mode` before considering this covered.
- Updated the two pre-existing gap-pinning tests in `test_tc18_gaps_ep2.c` (`test_mdio_decode_rejects_mms_mode_fails_closed`, `test_mdio_data_fields_are_unconditionally_sixteen_bit`) whose own doc comments had gone stale the moment this fix landed -- both still pass unchanged (they correctly pin permanent, by-design MMD-family behavior), only their comments needed correcting.
- 46 new tests (`test_ep_mdio.c`); 3 mutation tests (`rcp_ep_mdio_mms_uses_32bit_words()` forced false: 6 failures; `rcp_ep_mdio_mms_addr_valid()` forced true: 4 failures; the mode-routing guard disabled at both call sites: 2 failures after the missing write-side test was added). 114/114 native, 65/65 full suite both native and ASan/UBSan-clean. `cfusa check`: 0 errors. `cfusa trace`: 1024/1024 traced and tested (unchanged from baseline; a pre-existing, unrelated "dangling test reference" false-positive class -- 66 instances on unmodified `main`, for requirements independently confirmed to exist in `.fusa-reqs.json` -- is untouched by this batch and not investigated further here).

### v0.306.0 -- 2026-08-13 (`REQ-SEQ-013`: sequencer ownership access control, issue #335; bonus fix: real 2-octet `SEQUENCER_config` wire layout)

Closes `REQ-SEQ-013` (TC18 §12.7.10 Table 28's `Request_stream_index` field, naming the one RC Client permitted to access a given sequencer) with a real, layered, defense-in-depth fix, taken through three separate user-confirmation rounds before landing:

- **Ownership model**: `rcp_sequencer_table_t` (`request_sequencer.h`/`.c`) gained a parallel `owner[]` array alongside `state[]`, tracking each sequencer's claimant as a 1-based `request_stream_cfg` row index (`RCP_SEQUENCER_OWNER_UNCLAIMED == 0`), matching the same 1-based/0-sentinel convention `REQ-RMAP-052` already established. `rcp_sequencer_access_permitted()` is a pure predicate, fail-closed: an unclaimed sequencer is denied to *everyone*, not open to anyone, until an explicit claim -- the user's own choice among three presented options, since TC18 states the field's purpose but never how it is first populated.
- **Two independently-enforced attack vectors**, matching both halves of TC18's own vulnerability description: (1) direct EP0 register-map writes (`rcp_regmap_ep0_decode_write_request()`, now ownership-aware per octet -- `Seq_state` requires strict owner match, `Request_stream_index` itself permits unclaimed-or-owner-match so a client can claim an unclaimed sequencer or release/reassign one it already owns, this asymmetric rule being this implementation's own design choice, not TC18-mandated); and (2) the compound/compound-wait admission path, which never touches the register map at all (`mock.c`'s `dispatch_plain()`, admit-then-immediately-cancel via `rcp_server_endpoint_cancel_single()` on an unauthorized `sequencer_index`, responding `RCP_ERROR_UNAUTHORIZED_ACCESS`). Closing only the register-map vector would have left the more commonly-exercised protocol-native path wide open.
- **Full `request_stream_cfg` plumbing for `mock.c`** (the user's own explicitly chosen scope, over a narrower "just gate the register map" option): `rcp_mock_server_set_request_stream_cfg()` (new setter, mirroring the existing `sequencers`/`hw_pin_map` pattern) plus `rcp_regmap_request_stream_cfg_resolve_index()`, a `stream_id -> request_stream_index` resolver -- also unblocks `REQ-RMAP-048`/`REQ-RMAP-049`, not yet implemented.
- **Bonus conformance fix, found via direct primary-source verification** (not assumption): `REQ-SEQ-014`'s prior closure had wrongly claimed TC18's own `SEQUENCER_config` wire layout was 1 octet per sequencer, needing no `render()` step. A direct read of `TC18.txt` L3064-3110 (Table 28) shows it is actually **2 octets per sequencer** -- `Seq_state` then `Request_stream_index`, repeating. Any real client parsing the old 1-octet image per TC18's true 2-octet stride would misread every sequencer past the first. Fixed in the same batch: `rcp_regmap_sequencer_table_render()`/`_apply_reconfig()` now interleave both fields correctly, with the read/write EP0 dispatchers' extent-length math corrected to `sequencer_count * 2u`. `REQ-SEQ-014`'s own catalog text is corrected to match.
- Scope correctly excludes an orthogonal, pre-existing "no sequencer table configured at all" scenario (`rcp_sequencer_table_unsupported()`, `count == 0`): the new ownership gate is skipped entirely in that case, deferring to that scenario's own existing, separately-tested behavior (a request stays validly `PENDING`, never rejected) -- conflating the two would have wrongly regressed `test_compound_never_due_without_a_sequencer_table`.
- Test-file incident, worth recording honestly: a blind bulk find/replace against `tests/test_tc18_gaps_regmap.c`'s pre-existing lifecycle-state test arguments corrupted unrelated, already-passing test content. Recovered by fully reverting the file to clean HEAD (`git checkout --`) and redoing the entire sequence of mechanical edits from scratch, verifying exact match counts at each step before proceeding -- the same lesson this project has hit before with comment-aware bulk edits, reapplied here to test-argument bulk edits.
- 12 new tests: 6 at the `regmap.c`/EP0-dispatcher layer (unclaimed-denies, owner-permits, wrong-client-denies, claim-unclaimed-permits, steal-claimed-denies, owner-release-permits) plus 6 at the `request_sequencer.c` primitive layer; 4 new dedicated tests at the `mock.c`/dispatch-admission layer proving the compound/compound-wait rejection path explicitly (unclaimed-denies, wrong-owner-denies for both `COMPOUND` and `COMPOUND_WAIT`, and the `rcp_sequencer_table_unsupported()` scope-boundary case) -- the shared `fixture()` helper in `tests/test_conditional_dispatch.c` now claims all 4 sequencers for `stream_id=1` so its ~15 pre-existing compound/compound-wait tests (none of which exercise `REQ-SEQ-013`'s own access control) are not newly blocked by the fail-closed default. Two mutation tests confirmed both the `rcp_sequencer_access_permitted()` call and the `rcp_sequencer_table_unsupported()` guard are load-bearing: disabling either is caught by the new dedicated tests.
- ASan/UBSan-clean across the full suite; `cfusa check` 0 new errors; `cfusa trace` 1024/1024 traced and tested.

### v0.305.0 -- 2026-08-13 (`.fusa-reqs.json` staleness correction: `REQ-CFG-011`/`REQ-CFG-012` CAN's EP_func gap already closed)

**Doc-only.** `REQ-CFG-011` (generic §12.7.1 evt[2:0]==111b configuration mechanism reachable for every endpoint type) and `REQ-CFG-012` (EP_LEN + overrun-rejection present for every endpoint type) both flip `not-implemented` -> `implemented`. Both entries' own text still said CAN was the one exception — "no EP_func addressed-write path at all" / "no EP_LEN register or overrun-rejection path at all" — but `REQ-CANEP-028` (issue #201, 2026-08-12, landed the same day as the audit that wrote CFG-011/012's current text) already gave CAN exactly that: `rcp_ep_can_apply_reconfig()`, `can_ep_len` fixed at `RCP_EP_CAN_EP_FUNC_LEN`, and `RCP_EP_CAN_RECONFIG_ERR_OUT_OF_RANGE` on an out-of-range write. Nobody went back to re-check these two catalog entries after CANEP-028 landed — the same class of stale-precedent oversight already corrected once this session for `REQ-UART-032`.

Verified CAN's own bar matches the other ten endpoint types' exactly (not just superficially): `rcp_ep_gpio_apply_masked_write()`'s own doc comment confirms routing a decoded `evt[2:0]==111b` request to `apply_reconfig()` rather than the endpoint's normal read/write decoder is *every* endpoint module's caller's own responsibility, not something any module (including now CAN) wires into a live dispatch loop itself — so CFG-011/012 were never about a specific wired call site, only about the primitive existing and being reachable via the generic mechanism. CAN's own remaining gaps (`REQ-CANEP-029`'s acceptance-filter address collision; the bit-timing registers TC18 gives no sub-field layout for) are real but separately tracked and don't bear on either of these two requirements.

No code changed. `cfusa check`/`trace` re-run to confirm: identical to the pre-change baseline (0 errors, 100%/100% coverage). 65/65 both trees, unaffected.

### v0.304.0 -- 2026-08-13 (issue #334 batch 3: `REQ-RMAP-050` watchdog-timeout tick↔ms register wiring)

**`REQ-RMAP-050` flips `partial` -> `implemented`.** TC18 §12.7.7 Table 24's `rx_wd_timeout_intervall` register (relative address 0x000A, 16 bit, R/W*, "WatchDog time out for this Stream in clock tics") had a conversion pair already implemented and unit-tested (`rcp_regmap_wd_timeout_ms_to_ticks()`/`_ticks_to_ms()`) but no register-write code path called them -- the register's own two octets always rendered as a hardcoded `0x0000` and a write landing on them was silently discarded.

**Asked for explicit sign-off before implementing** (this register gates watchdog safe-state entry, ASIL-relevant, and an earlier pass in this codebase had already flagged the fail-safe direction as "not a judgment this library should make unilaterally"). On closer inspection, TC18's own "a written value shall be rejected if it does not fit the register's 16-bit width" rule turned out to be about validating a value before accepting it into `rx_wd_timeout_ms` (the READ/render direction) -- not the WRITE/apply_reconfig direction, where an arriving wire value is always exactly 16 bits by construction and can never itself violate a width constraint. User approved: caller-configurable `ms_per_tick`, reject-not-saturate philosophy (already exactly what the existing conversion primitives do).

**Mechanism**: `rcp_regmap_request_stream_cfg_render()`/`_apply_reconfig()`, and the EP0 dispatchers that call them (`rcp_regmap_ep0_decode_write_request()`/`_encode_read_response()`), all gained a new trailing `watchdog_ms_per_tick` parameter -- TC18 names no fixed clock-tick rate for this register anywhere, so, matching this file's own established "caller supplies already-classified units" convention, the caller supplies it. `0` means "not configured": both conversion primitives already reject `ms_per_tick == 0` on their own, so this fails closed with no separate sentinel needed. On render, a value that can't be represented in 16-bit ticks (unconfigured rate, or a value that's simply too large) falls back to encoding `0x0000` -- the same "reserved / cannot be represented, use 0" treatment `REQ-RMAP-024`'s own HW_config alignment octet already established. On apply_reconfig, a ticks-to-ms conversion failure leaves `rx_wd_timeout_ms` unchanged (not clobbered to some meaningless derived value) rather than failing the whole multi-field write.

~50 existing call sites in `test_tc18_gaps_regmap.c` updated to pass `0u` (preserving every existing test's assertions exactly, since `ms_per_tick == 0` reproduces the prior "always 0x0000, always unchanged" behavior byte-for-byte) via a comment-aware Python script (tracks `/* */`/`//`/string-literal state properly this time, after an earlier draft of the same technique in this session's history corrupted 6 doc-comment function references by blindly appending into every textual occurrence of a function name -- caught before commit by re-grepping for the resulting `(, 0u)` garbage pattern). 5 new dedicated tests cover the real configured-rate path in both directions (render success, render fallback-when-still-too-large, apply_reconfig success, apply_reconfig fallback-when-unconfigured) plus the pre-existing zero-fallback test renamed for clarity. 2 mutation tests (render always-falls-back, apply_reconfig always-assigns-ignoring-failure) both caught cleanly.

**Tooling note**: `cfusa check` A/B (CI-pinned `v0.5.50` binary) first showed 2 new errors -- a third self-inflicted `CFUSA-CY009` false positive this lineage has now hit (a test named `..._encodes_real_ticks_when...` contained the literal substring `des_` inside "en**codes\_**real", tripping the same naive substring-match weak-crypto checker RMAP's own audit lineage hit once before). Renamed to `..._produces_real_ticks_when...`; re-check confirmed 0 new errors -- in fact one fewer `CFUSA-A006` pointer-arithmetic advisory than baseline in both touched files (render()'s new `put_u16()` call replaced a two-statement inline zero-write the checker had been matching). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100% (1024/1024). 65/65 both trees (native + ASan/UBSan).

### v0.303.0 -- 2026-08-13 (issue #334 batch 2: `REQ-WDG-010` per-stream watchdog kick on the plain dispatch path)

**`REQ-WDG-010` flips `partial` -> `implemented`.** TC18 §12.7.7: "the watchdog is reset with each request received from this RC Client." The E2E-aware dispatch functions (`rcp_mock_server_dispatch_e2e()`/`_dispatch_frame_e2e()`) already kicked correctly (issue #201); the plain, non-E2E `rcp_mock_server_dispatch()`/`_dispatch_frame()` had **no `stream_id` parameter at all** to key a kick by -- a real gap this codebase's own `.fusa-reqs.json` text had previously flagged as deliberately deferred because "widening its own signature would ripple across every existing call site... far beyond this fix's narrow scope."

**Primary-source check before widening**: verified against TC18 §13.6 that "plain command mode" vs "safe command mode" is only about whether CRC32 E2E protection is applied -- both modes are carried inside the same NTSCF/TSCF AVTPDU, which always has a real `stream_id` field regardless. So the API gap was real, not a "doesn't apply here" case, and both dispatch functions were widened to take an explicit `stream_id` parameter, matching `_dispatch_e2e()`'s own existing parameter ordering.

**No double-kick**: `rcp_mock_server_dispatch_e2e()` already kicks once, unconditionally, at its own top (covering CRC-mismatch/short-frame paths that return before ever reaching plain dispatch). Its own two delegation call sites now route through a new internal `dispatch_plain()` helper (the old `rcp_mock_server_dispatch()` body, factored out) rather than the public `rcp_mock_server_dispatch()` wrapper -- which, as of this fix, kicks on its own -- so a request already kicked once by `dispatch_e2e()` is never kicked a second time for the same receipt.

All ~25 existing call sites across `test_mock.c`/`test_conditional_dispatch.c` updated to pass a `stream_id` argument. Two new tests (`test_mock.c`, mirroring `test_tc18_gaps_e2e.c`'s own existing E2E watchdog tests): `test_dispatch_kicks_the_watchdog_on_every_admitted_request` (a 40 ms timeout survives being dispatched every 10 ms for 100 ms) and `test_dispatch_kicks_the_watchdog_even_when_the_request_is_rejected` (a rejected -- not executed -- request still kicks, TC18's own "receipt not validation" rule). 2 mutation tests (kick-only-on-success, no-kick-at-all) both caught cleanly.

**Deliberately still out of scope, not a remaining gap in this request-reception path**: `server.h`'s own core `rcp_server_endpoint_submit()` (the lowest-level receive path, exercised directly by `test_tc18_gaps_server.c`'s own `test_watchdog_overflows_despite_continuous_requests()`) has no `stream_id` concept at all -- `server.h` operates on one `rcp_server_endpoint_t`, not a multi-endpoint RC Server, and is deliberately layered below the AVTP/stream concept entirely. A caller reaching an endpoint through that primitive directly, bypassing the reference server's own dispatch functions, is already bypassing every other stream-scoped RC-Client behavior along with the watchdog, not uniquely this one.

**Tooling note**: `cfusa check` A/B (correct CI-pinned `v0.5.50` binary, per the previous batch's own lesson) first showed 1 new error -- a genuine (not stale-binary) `CFUSA-L004` "appears recursive" false positive, this time self-inflicted by a doc comment inside the new `wdg_busy_wait_ms()` helper's own body that literally spelled `wdg_busy_wait_ms(` (referencing its identically-named twin in another test file). Reworded to avoid the literal `name(` pattern; re-check confirmed 0 new errors. Normalized diff shows only the expected +3 `CFUSA-CY006`/+3 `CFUSA-L003` (malloc/free-related, from the new watchdog-keeper test fixtures) in `test_mock.c`. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100% (1024/1024). 65/65 both trees (native + ASan/UBSan).

### v0.302.0 -- 2026-08-13 (issue #334 batch 1: `REQ-CANCEL-012` chain-cascade cancellation)

**`rcp_server_endpoint_cancel_chain_from()`** (`server.h`/`server.c`) closes the first item of issue #334's deferred-dispatch-wiring backlog: TC18 §11.2.3's cascade rule -- "If a request is cancelled to which a request is chained, then the chained successors shall be cancelled by the RC Server as well" -- was already correctly implemented and unit-tested as a standalone predicate (`rcp_cancel_chain_should_cascade()`, `request_cancel.c`) but had zero callers anywhere in `src/` (issue #256 Group H's original finding, reconfirmed by issue #334's own audit).

Two new fields on `rcp_server_pending_t` (`chain_group`/`chain_position`) record each pending conditional request's own position within whichever chain it was admitted as part of -- chain membership is a property of the enclosing frame, not of any member's own wire fields (`request_chained.h`), so `mock.c`'s own `dispatch_frame()`/`dispatch_frame_e2e()` loops are the only place with the context needed to derive it; `chain_group == 0` is the reserved "not part of a chain" sentinel every non-chained entry carries by default. `apply_cancellation()`'s `CLEAR_SINGLE` path now reads the target's `chain_group`/`chain_position` out of the pending store before cancelling it (cancellation frees the slot), then calls the new cascade primitive so every chained successor at or after the cancelled member's own position is removed too -- a target with no chain membership (`chain_group == 0`) makes the cascade call a guaranteed no-op, exactly matching an already-not-found target.

Deliberately **not** fully closed: each cascaded removal is, by the same TC18 §11.2.3 rule, its own `REQUEST_CANCELED` response -- but `apply_cancellation()`'s single `out_response` parameter can only carry one response per call, the identical multi-response-fanout limitation already tracked for clear-all/clear-non-safestate under issue #163. Not attempted here; scope stays aligned with that existing, separately-tracked boundary.

Two new integration tests (`test_conditional_dispatch.c`): cancelling a chain's first member removes both it and its chained successor; cancelling one member of two independent back-to-back chains in the same frame leaves the other chain's member untouched (proves `chain_group` isolation, not mere frame-position). 3 mutation tests confirm the cascade logic actually gates on both fields.

`cfusa check` A/B (rebuilt at the CI-pinned `v0.5.50` tag after discovering this session's own locally cached binary predated c-FuSa's upstream `CFUSA-L004` false-positive fix from v0.5.39 -- see `ROADMAP.md` for the full story): **0 errors, both before and after.** Normalized diff (rule+file, ignoring line-number reshuffling from the insertion) shows exactly 4 real deltas, all confined to the two new tests' own buffer-building code in `test_conditional_dispatch.c` -- `CFUSA-CY001`/`CY006`/`L003` (memcpy/free/heap-usage advisories, +7/+9/+9) and `CFUSA-L001` (+2, the two new tests run 51 and 55 lines against a 50-line advisory threshold). `src/server.h`, `src/server.c`, and `src/mock.c` carry zero new findings of any severity. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100% (1024/1024). Full native (65/65) and ASan/UBSan (65/65) suites clean.

### v0.301.0 -- 2026-08-13 (no-status-field audit: all 862 unstatused `.fusa-reqs.json` entries verified against the TC18 PDF and the code)

**Doc-only.** Every `.fusa-reqs.json` entry that had never carried a `status` field (862 of 1069 total, predating that field's introduction) has now been individually checked against either the TC18 spec (for the 497 carrying a real `tc18` citation) or the code alone (for the 365 pure implementation-behavior entries -- internal helpers, satellite/bridge modules, RELAY-spec-level concerns -- with no TC18 basis to check against). Run as a 51-batch parallel verify pass (grouped by requirement-id module, largest modules split further to keep each batch reviewable) followed by an adversarial recheck pass on every finding that came back non-`implemented`, proposed a citation/text correction, or carried low confidence (58 of 862, ~7%) -- a second, independently-instructed agent per flagged finding, explicitly told to try to refute the first pass rather than confirm it.

**Result: 827 `implemented`, 35 `partial`, 0 `not-implemented`.** Zero of these 862 turned out to be genuinely missing behavior -- everything either checks out in full or is real-but-incomplete (untested, partially wired, or documented with a known caveat already in its own text).

**The recheck pass earned its cost twice**, both flipping a first-pass verdict:
- `REQ-CANCEL-001` (`partial` -> `implemented`): first pass missed that `rcp_cancel_strerror()`'s full 7-value switch is directly tested; recheck found the covering test.
- `REQ-LIFECYCLE-023` (`implemented` -> `partial`, the more consequential of the two): first pass accepted this requirement's own long-standing claim that `RCP_LIFECYCLE_FIELD_HW_GENERIC`'s write-lock rule already covers `ep_generic_cfg`/`request_stream_cfg`/`response_queue_cfg`, not just HW pin-mapping. Recheck traced the *actual* EP0 write dispatcher (`src/regmap.c`) and found those three tables are gated by `FUNCTIONAL_W_STAR`/`W_PLUS` (writable during `HW_CONFIGURED`), never `HW_GENERIC` (writable only during `HW_UNCONFIGURED`) -- the opposite locking behavior from what this requirement claimed, and in direct, previously-unnoticed conflict with `REQ-RMAP-047`'s own already-correct text. Root cause: this requirement's claim predates the EP0 dispatcher work (issue #301/#306/#308/#311) that later reached the opposite conclusion; nobody reconciled the two. Rewrote this entry's text in full to document the real, contradictory state honestly rather than force a resolution -- a dedicated pass should decide whether to retarget it to HW_config alone or change `src/regmap.c`'s actual locking behavior, the latter being a real code change, not a documentation fix.

**91 stale/wrong TC18 citations corrected**, the large majority a single root cause: TC18.txt (this repo's plaintext extraction reference) was pulled from an older PDF revision (2026-07-14 footer) than the currently authoritative one (`OA_TC18_specification_v_0.5.1_RC_5_3624.pdf`, 2026-07-31 footer); the newer revision gained front-matter/content that shifted figure and table numbers (e.g. Figure 13->14, Table 11->13 for `REQ-CANCEL-002`'s own citation) without changing the underlying diagram/table content -- the same class of drift already tracked under issue #341, now confirmed to extend well beyond that issue's own partial census. **21 stale requirement titles/texts corrected** (wrong function names, wrong requirement-id cross-references, "nibble" vs "field" terminology drift, citations to tables later renumbered by this codebase's own earlier fixes) -- all verified against the current code before rewriting, none invented.

**One item deliberately left as an open question, not force-resolved**: `REQ-E2E-012`/`REQ-E2E-013` (`rcp_e2e_request_may_execute()`'s safety-gating behavior) both independently flagged low confidence on recheck -- the code matches its own requirement text and passes its tests (so `implemented` stands), but the exact TC18 §11.2.2 sentence this pair's citation is built on ("...will only be executed when the EP needs to go to safe state...") has been removed from the current 0.5.1_RC5 PDF and replaced with narrower purge-survival language that arguably describes a different mechanism (`REQ-E2E-014`/`015`'s own territory) than execution-admission. Not reinterpreted here, matching this codebase's own standing policy of not unilaterally reinterpreting safety-relevant TC18 semantics without dedicated investigation and sign-off.

**Verification**: `cfusa check` A/B (git-stash, byte-for-byte score comparison): identical, `errors=167 warnings=1701 info=1209` both before and after -- expected for a pure `.fusa-reqs.json` text/status change. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024, unaffected. 65/65 both trees (native + ASan/UBSan), fresh clean rebuild -- unaffected, as expected for a docs-only change, run anyway per this project's own standing discipline. No stray iCloud-sync duplicate files.

### v0.300.0 -- 2026-08-13 (issue #201/#336: `REQ-E2E-046`, `rx_stream_status` aggregate blocked-status latch -- `not-implemented` -> `partial`)

**`REQ-E2E-046` flips `not-implemented` -> `partial`.**

TC18 0.5.1_RC5's own Table 24 (§12.7.7) adds a new read-only `rx_stream_status` bit at `0x000D.7` with no counterpart in the baseline this codebase was originally built against: "0b: stream is active / 1b: stream is blocked, requests are rejected", set automatically as a reaction to a CRC error, sequence error, watchdog overflow, or request-storage overflow, whichever of those four is enabled. This is a passive, client-polled *aggregate* status, distinct from each cause's own existing per-call "should enter safe state now" decision (`rcp_e2e_seq_evaluate()`'s/`rcp_e2e_wd_evaluate()`'s `result.enter_safe_state`, `rcp_e2e_overflow_should_enter_safe_state()`'s return value) -- those report a one-shot verdict at the instant a fault is evaluated; `rx_stream_status` is instead a *persisted* "is the stream currently blocked" state a client can poll at any later time, the same shape `rcp_e2e_stream_fault_t` already established for the CRC cause alone. regmap.h's own "TC18 0.5.1_RC5 terminology drift" section had explicitly named this cross-cutting aggregate-latch primitive as the one piece of that investigation left genuinely unresolved (task #97).

New `rcp_e2e_stream_status_t` (`e2e.h`/`e2e.c`) reuses `rcp_e2e_stream_fault_t` unchanged for the CRC cause (composition, not duplication) and adds three sibling bool latches (`seq_blocked`, `wd_blocked`, `overflow_blocked`) of the identical shape for the other three fault classes. `rcp_e2e_stream_status_note_crc_error()`/`_note_seq()`/`_note_wd()`/`_note_overflow()` latch each cause from a result the caller already computed via this module's own existing evaluators -- composed, not re-derived, the same "own small pure/stateful helpers, operate on caller-supplied results" layering discipline every other latch in this module already follows. `rcp_e2e_stream_status_rx_blocked()` is the pure aggregate read: true iff *any* of the four latches is currently set -- TC18's own "either...or...or...or" wording read as a logical OR, the same reading this module's own crc/seq/wd/overflow evaluators already use independently of one another. Each cause has its own independent reset (`rcp_e2e_stream_status_reset_crc()`/`_seq()`/`_wd()`/`_overflow()`), since TC18 gives each of the four fault classes its own distinct release condition with no basis to assume clearing one also clears another's.

**Stays `partial`**: `src/mock.c` has no per-endpoint-type dispatch of any kind (confirmed via direct grep) to actually call these `note_*()` functions from a live evaluate()-then-latch path, or to expose `rcp_e2e_stream_status_rx_blocked()` as a real register read -- the same disposition already established for `REQ-CANCEL-012`/`REQ-ADC-037`/`REQ-TIMED-012`/`REQ-GPIO-035`/`REQ-GPIO-036`/`REQ-CANEP-030`/`REQ-ISELED-025`.

**Verification**: 7 new unit tests in `tests/test_e2e.c` (init state, each of the four causes independently blocking/resetting, and a dedicated cross-cause independence test proving resetting one latch never clears a different, still-latched cause). Mutation-tested 3 ways (the aggregate OR flipped to AND, `note_seq()`'s gate condition swapped from `enter_safe_state` to the weaker `discontinuity`, `reset_crc()` made to also bleed into `seq_blocked`) -- all caught cleanly. `cfusa check` A/B, normalized by finding text: **0 new findings at all** -- errors/warnings/info identical (167/1701/1209 both before and after); the only textual diff was a single pre-existing "function too long" MISRA advisory on `main()` shifting from 62 to 70 lines (7 new `RUN_TEST` calls), not a new finding class. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024. 65/65 both trees (native + ASan/UBSan).

### v0.299.0 -- 2026-08-13 (issue #336: `REQ-ISELED-025`, ISELED response fragmentation -- `not-implemented` -> `partial`)

**`REQ-ISELED-025` flips `not-implemented` -> `partial`.**

TC18 §13.7.12.1 requires that ISELED read responses "are collected 5/4bit decoded and aggregated into one or multiple ACF type up to the requested read_size" -- a ceiling-then-fragment rule this module never implemented: `rcp_ep_iseled_encode_response()` encoded exactly the `rx_data`/`rx_len` its caller supplied, never read a `read_size`, and had no multi-message emission path.

New `rcp_ep_iseled_response_fragment_count()`/`rcp_ep_iseled_encode_response_fragmented()` (`ep_iseled.h`/`ep_iseled.c`) close this by reusing the codebase's existing generic `fragment.h` module rather than inventing an ISELED-specific scheme -- the same module `ep_can.c`'s own frame-response fragmentation already integrates against. `read_size` is applied as a ceiling first (`min(available_len, read_size)`), then `rcp_fragment_plan_count()`/`rcp_fragment_plan()` divide the capped payload into fragments, each emitted as its own ABB (untimed) or GBB (timed) ACF message with `ms`/`read_size_or_segment_num` populated for multi-segment output -- mirroring `rcp_ep_can_encode_frame_response_fragmented()`'s own established header-field conventions exactly.

Deliberately **no fragment-aware decode counterpart** was added, unlike CAN's own `decode_frame_response_fragment()`: ISELED's response payload has no embedded leading-quadlet structure a fragment boundary could split awkwardly, so the pre-existing, unmodified `rcp_ep_iseled_decode_response()` already works unchanged as a per-fragment decoder -- confirmed directly by a full encode/fragment/decode/reassemble round-trip test that respects the `read_size` ceiling even when more source data was actually available.

**Stays `partial`**: `src/mock.c` has no ISELED-specific dispatch of any kind (confirmed via direct grep), so nothing in a live request/response path calls either new function yet -- the same disposition already established for `REQ-CANCEL-012`/`REQ-ADC-037`/`REQ-TIMED-012`/`REQ-GPIO-035`/`REQ-GPIO-036`/`REQ-CANEP-030`. `REQ-ISELED-027`'s own cross-reference to this requirement updated to match.

**Verification**: 4 new unit tests in `tests/test_ep_iseled.c`, including the full worst-case round trip described above. Mutation-tested 3 ways (the `read_size` ceiling's ternary direction, the untimed-frame `ms` bit assignment, an off-by-one dropping the final fragment from the encode loop) -- all caught cleanly, the last as a segfault (out-of-bounds `out_frames[]` write left uninitialized by the shortened loop, caught by the test harness's own bounds). `cfusa check` A/B, normalized by finding text: **0 new errors** (167 both before and after -- the actual merge gate), +9 warnings/+8 info, all individually reviewed and expected: `malloc`/`free`-related CWE-190/CWE-416/MISRA-21.3 advisories from the two `malloc(segs)`/`free(segs)` sites (same finding class already present at `rcp_ep_can_encode_frame_response_fragmented()`'s own equivalent `fragment.h` integration), pointer-arithmetic/signed-unsigned-with-`sizeof` advisories from the same new loop, two "function is N lines (max 50)" MISRA advisories (the new round-trip test itself, and `main()` growing from 65 to 70 lines from 4 new `RUN_TEST` calls), and a handful of `CFUSA-A006`/`CFUSA-CY006`/`CFUSA-A003`/`CFUSA-CY005`/`CFUSA-L001`/`CFUSA-L003` informational/style findings on the new functions themselves. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024. 65/65 both trees (native + ASan/UBSan).

### v0.298.0 -- 2026-08-13 (issue #336: `REQ-CANEP-030`, CAN XL physical-layer provisioning -- `not-implemented` -> `partial`)

**`REQ-CANEP-030` flips `not-implemented` -> `partial`.**

TC18 §13.7.11.2 lists "usage of new PL (YES|NO) for CAN XL" among the settings the CAN endpoint's functional configuration comprises, alongside bit-rate and filter settings that all have real register rows in Table 56. This one does not: the register table goes directly from `can_clk_divider` (0x0008) to the undecomposed CAN bit-time/TDCC register span (0x000C-0x001B) with no row named for this setting anywhere. This is a genuine specification gap, not a local implementation one -- filed as canonical spec-defects report item 57 (`TC18_spec_defects_report.md` and its `_quadruple_checked.md` review copy).

New `rcp_ep_can_functional_cfg_t::xl_new_pl_provisioned` (`ep_can.h`/`ep_can.c`) is deliberately an **in-memory-only** field with no wire offset, matching that constraint honestly rather than inventing an unverified register bit. `rcp_ep_can_set_xl_new_pl_provisioned()` lets a caller record this choice (lifecycle-authorized, same as every other functional-config setter); new `rcp_ep_can_xl_frame_matches_provisioned_pl()` validates a decoded frame's own XL variant against it -- closing this requirement's own "nothing rejects a frame whose XL variant contradicts the endpoint's actual physical layer" half. c-RCP's own pre-existing per-frame expression of physical-layer selection (`RCP_EP_CAN_FRAME_XL_CLASSICAL_PL`/`_NEW_PL` in evt[2:0]) is unchanged.

**Stays `partial`**: a client still cannot read this setting back over the register map the way every other functional-config setting can, since TC18 gives no bit position to expose it at.

**Verification**: 4 new unit tests in `tests/test_ep_can.c`. Mutation-tested 3 ways (the provisioned-PL comparison, the non-XL early-return, the new setter's own authorization gate) -- all caught cleanly. `cfusa check` A/B: zero new or removed findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024. 65/65 both trees (native + ASan/UBSan).

### v0.297.0 -- 2026-08-13 (issue #336: `REQ-GPIO-035`/`REQ-GPIO-036` debounce-filter and response-timing primitives)

**`REQ-GPIO-035`'s remaining debounce-filtering half, and `REQ-GPIO-036` in full, now have real, tested decision primitives -- both stay `partial`.**

TC18 §13.7.4.2 Table 41's `gpio_debounce_IOn` rule ("0: no debounce; n>0: n consecutive samples of the same value need to be sampled before the output value is changed") had storage but no filtering logic. New `rcp_ep_gpio_debounce_state_t`/`rcp_ep_gpio_debounce_sample()` implement that rule as a caller-owned tracker, matching every other stateful primitive in this codebase (`rcp_ep_adc_trigger_state_t` et al.) -- directly unit-tested for the settle threshold, the differing-sample run-reset rule ("n CONSECUTIVE", not merely n at any point), first-settle-is-not-a-change, and the pre-settle `false` default (deliberately not leaking raw, unfiltered samples, which would defeat the filter's own purpose).

TC18 §13.7.4.3's GPIO response-timing rule (immediate for a pure read, post-debounce for a payload-bearing read or any write) had no classifier at all. New `rcp_ep_gpio_response_timing()` is that pure classification, decided from a decoded request's own `op` and `payload_len` -- both already available to any caller that has decoded the ACF header, so no change to either existing decoder's own signature was needed.

**Deliberately stays `partial`, not `implemented`, for two reasons**: (1) `gpio_base_clk` remains read-only and always renders 0 -- the same "no real clock source modelled" architecture-wide constant already established for `REQ-ADC-033`/every other endpoint type's own `base_clk` field -- so the periodic sampling cadence that would drive repeated debounce calls remains a caller-owned timer this module never itself runs; (2) `src/mock.c` has no per-endpoint-type dispatch of any kind, so neither new function is yet called by any real dispatch path -- the same disposition already established for `REQ-CANCEL-012`/`REQ-ADC-037`/`REQ-TIMED-012`.

**Verification**: 10 new unit tests in `tests/test_ep_gpio.c` (one of the author's own test assertions was itself initially wrong -- `test_debounce_zero_means_no_debounce` asserted `FALSE` for a `true` first sample with `n=0`, caught immediately on the first test run and corrected, not an implementation bug). Mutation-tested 3 ways (the settle-threshold boundary, the run-reset-on-differing-sample condition, the payload-bearing-read classification) -- all caught cleanly. `cfusa check` A/B, normalized by finding text: zero new or removed findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected). 65/65 both trees (native + ASan/UBSan).

### v0.296.0 -- 2026-08-13 (issue #336 catalog-drift correction: `REQ-ADC-034` flips `not-implemented` -> `implemented`)

**`REQ-ADC-034` flips `not-implemented` -> `implemented`.** This requirement's own text was stale on two independent counts:

1. Its own precedent citation -- "contrast ep_lin.h's `rcp_ep_lin_compare_fires()`, `REQ-LINEP-002..005`" -- names a function and requirement ids that do not exist anywhere in this codebase. The real, existing mechanism is `acf.h`'s `rcp_acf_compound_wait_match()`, a universal, endpoint-agnostic comparator wired into real dispatch via `server.c`'s `rcp_server_tick_ctx_t.current_status` -- exactly the same mechanism `REQ-UART-035` already corrected an identical stale claim for. No ADC-specific comparator was ever needed. `tests/test_acf.c` already carries 45+ dedicated assertions for this shared mechanism.
2. The requirement's other half -- sampling only while a request executes, so no trigger fires absent a request -- is genuinely out of scope by this module's own documented design: `ep_adc.h`'s file header states "this module never itself owns a timer, thread, or background sampling loop", so there is no c-RCP-owned sampling loop for any caller-side gating rule to apply to. `rcp_ep_adc_trigger_evaluate()` (`REQ-ADC-031`) already gives the caller the trigger-firing decision itself.

Renamed the stale deviation-pin test in `tests/test_tc18_gaps_ep2.c` (`test_adc_has_no_trigger_outputs_and_no_retained_average` -> `test_adc_pipeline_is_stateless_by_design_and_cadence_deviation_pin`) and rewrote both its comment blocks -- the first also corrected a second, independent staleness (`REQ-ADC-031` already implemented Table 50's trigger outputs; the old comment predated it).

Doc-only, no functional code change. `cfusa check` A/B, normalized by finding text: zero new or removed findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected). 65/65 both trees (native + ASan/UBSan).

### v0.295.0 -- 2026-08-12 (issue #336/#338: `REQ-SPI-037` genuinely blocked by TC18 spec silence -- documented, not force-implemented)

**Doc-only, no code change.** Investigated `REQ-SPI-037`'s "SPI stops execution (see cs/hs bits) -- enter error state, reset EP_config enable bit" rule (TC18 §13.7.3.3) and confirmed a real specification defect, not a local implementation gap: no table anywhere in the document gives SPI's own `cs`/`hs` ACF header bits a "stopped/errored" meaning -- every standard-request table lists both as fixed `0b`/reserved, and the only request kind giving `cs` a real meaning (Chained requests, "Conditional start") has no connection to SPI's own execution model. It is equally plausible the sentence instead means the SPI bus's own physical CS/HS hardware signal lines (a distinct, electrical-level concept §13.7.3.1's own Table 38 already names "CS0"-"CS5"), not the ACF protocol header bits at all -- the two readings are not equivalent and lead to entirely different implementations.

Filed as canonical spec-defects report item 56 (`TC18_spec_defects_report.md` and its `_quadruple_checked.md` review copy), matching item 55's own earlier precedent. `.fusa-reqs.json` updated to document the real blocking reason (status stays `not-implemented`, same disposition already established for `REQ-PWM-057`'s own TC18-spec-defect block) -- the requirement's second half (clamped-pin diagnostic flag) is left unattempted alongside it rather than force-split into a differently-dispositioned entry, since the underlying pin-electrical-state detection is hardware this module has never modelled for any endpoint type either.

`cfusa check` A/B: byte-identical (only the timestamp line differs). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected -- in-place text edit, status unchanged). 65/65 both trees (unaffected, as expected for a docs-only change).

### v0.294.0 -- 2026-08-12 (issue #336: `REQ-TIMED-012` TSCF timestamp-extension primitive -- `not-implemented` -> `partial`)

**`REQ-TIMED-012` flips `not-implemented` -> `partial`.**

TC18 §11.2/§11.2.1 requires that any request carried under a TSCF header -- standard, conditional, or cancel, not just `request_timed.h`'s own Timed request kind -- is postponed until the header's own `avtp_timestamp` presentation time. `avtp_timestamp` is a 32-bit, nanoseconds-modulo-2^32 IEEE 1722 field, while the admission/due-selection path (`rcp_timed_due()`, `rcp_server_endpoint_select_due()`) operates entirely in the 48-bit gPTP-domain clock `request_timed.h`'s own `presentation_time` already uses. Comparing the two directly is unsound: a 32-bit field cannot itself carry which of the many congruent 48-bit instants was intended, and naive zero-extension misreads a request meant ~100ms in the future as ~4.29s in the past whenever `avtp_timestamp`'s own low bits are numerically smaller than the current clock's.

New `rcp_avtp_extend_timestamp()` (`avtp.h`/`avtp.c`) resolves that reconstruction correctly -- the same nearest-candidate technique every real IEEE 1722/AVTP receiver uses, not a c-RCP invention -- returning the 48-bit-domain instant closest to a caller-supplied reference clock, directly composable with the existing `rcp_timed_due()` comparison exactly as `request_timed.h`'s own `presentation_time` already is.

**Deliberately does not close the requirement fully**: `rcp_server_endpoint_admit()`'s own public signature has no parameter carrying a TSCF header's `tv`/`avtp_timestamp` at all (it operates on the post-AVTPDU-unwrap ACF frame only), so no real dispatch path in this codebase yet applies this postponement universally across every request kind. That wiring (a new `admit()`/`dispatch()` parameter, a new per-slot due-time field, and a new envelope-level gate check alongside each kind's own existing condition) is real, additional, API-surface-changing work, tracked as this requirement's own remaining scope. `request_timed.h`'s own Timed-kind `presentation_time` mechanism continues to work exactly as before and is unaffected.

**Verification**: 7 new unit tests in `tests/test_avtp.c` (exact match, near-future/near-past without wraparound, forward and backward period-boundary wraparound, both half-period tie-break boundaries pinned explicitly, and an end-to-end walk through `rcp_timed_due()`). Mutation-tested 3 ways (both half-period boundary comparisons, the wrap-direction sign) -- **the backward-boundary mutation was NOT caught by the first test pass**, a genuine coverage gap (only the forward half-period boundary had a dedicated pin): fixed by adding `test_extend_timestamp_exactly_half_period_backward_prefers_no_wrap`, confirmed to catch the mutation afterward. `cfusa check` A/B, normalized by finding text: zero new or removed findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected -- in-place text/status edit). 65/65 both trees (native + ASan/UBSan).

### v0.293.0 -- 2026-08-12 (issue #336: `REQ-ADC-037` cadence-decision primitives -- `not-implemented` -> `partial`)

**`REQ-ADC-037` flips `not-implemented` -> `partial`.**

TC18 §13.7.9.2 states three cadence cases comparing `adc_combine_avg_values` against `adc_avg_intervals_per_request` (accumulate several request executions into one response; one response per execution; fan one execution out across several responses), but no function anywhere in this module decided which case applied or when enough values had accumulated for a response. New `rcp_ep_adc_cadence_case()` (`ep_adc.h`/`ep_adc.c`) classifies the three cases; new `rcp_ep_adc_cadence_response_ready()` answers the single comparison underlying all three (`pending_value_count >= combine_avg_values`) -- directly unit-tested, including both boundary conditions and an end-to-end walk of the ACCUMULATE and FAN_OUT cases across multiple simulated executions/responses.

**Deliberately does not close the requirement fully, for two honest reasons, both spelled out in `.fusa-reqs.json`:**

1. Assembling a ready response's own value array and computing its `transaction_num` remain the caller's own bookkeeping, matching `rcp_ep_adc_collect_response_values()`'s own "operates on caller-supplied arrays, owns no sample storage" scope -- TC18 gives no instruction for how a caller should track per-value provenance across executions, and this module correctly does not invent one.
2. Unlike `REQ-E2E-021`'s own precedent for this exact class of gap, no real dispatch path exists to wire this into: `src/mock.c` has no per-endpoint-type dispatch of any kind (ADC included), so these two functions are correct and tested but not yet exercised end-to-end by this codebase's own reference server -- the same disposition `REQ-CANCEL-012` was left at for an analogous reason.

Renamed and rewrote the deviation-pin block in `tests/test_tc18_gaps_ep2.c` to document the narrowed (not closed) gap.

**`cfusa check` A/B, normalized by finding text/CWE (not raw file:line, since the new test functions shift every later line number in the same file)**: the one genuinely new finding is `rcp_ep_adc_cadence_case()` triggering `CFUSA-L004`'s "appears recursive" (MISRA-C 2012 Rule 17.2) heuristic -- confirmed as the *exact same pre-existing false-positive class* already present ~158 times across this codebase (e.g. `rcp_ep_pwm_out_apply_write`, `rcp_acf_compound_wait_match`), not a real recursion (the function is a straightforward two-branch if/return chain) and not a new category of finding CI doesn't already tolerate. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected -- in-place text/status edit). 3/3 mutations caught cleanly (both `cadence_case()` boundaries, the `response_ready()` boundary). 65/65 both trees (native + ASan/UBSan).

### v0.292.0 -- 2026-08-12 (issue #336 catalog-drift correction: `REQ-UART-032` flips `not-implemented` -> `implemented`)

**Doc-only, no functional code change.** `REQ-UART-032`'s own "NOT IMPLEMENTED" text was stale the day it was filed: `rcp_ep_uart_functional_cfg_t::ep_status` (`uart_ep_status`, Table 48 `0x0004`, 16-bit R/W) has existed as a real, freely-settable, round-tripped register field since PR #276 (issue #256, 2026-08-11) -- the day *before* this requirement was filed against a stale reading of the code during the 2026-08-12 gap audit.

TC18 §13.7.8.1 requires an RX FIFO overflow to be "flagged in the UART EP status register" but never defines which bit of that 16-bit register carries the flag -- the same `_ep_status` spec-silence pattern already accepted for CAN/WakeUp/several other endpoint types' own status registers (`REQ-CANEP-028` reached the identical disposition one batch earlier: register real, freely-settable, round-tripped, no bit position invented). Consistent with that precedent, `uart_ep_status` correctly does not invent an overflow bit -- it stores and round-trips whatever value a caller or register-map write assigns, matching every other endpoint type's own status register.

Renamed the deviation-pin test in `tests/test_tc18_gaps_ep2.c` (`test_uart_rx_fifo_size_bounds_nothing_at_all` -> `test_uart_rx_fifo_size_bounds_nothing_overflow_flag_left_uninterpreted`) and rewrote its comment to document the corrected disposition rather than a "still open" framing that no longer matched the code. No new test needed -- `tests/test_ep_uart.c`'s own register-block round-trip test already positively covers `ep_status`'s wire round-trip.

`cfusa check` A/B (`git stash`): every diff hunk is a pure line-number shift from the comment rewrite itself (identical finding text/CWE/rule ids, same count) -- zero new or removed findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%, 1024/1024 (unaffected -- in-place text/status edit, no entries added/removed). 65/65 both trees (native + ASan/UBSan).

### v0.291.0 -- 2026-08-12 (MDIO catalog cleanup: 19 entries missing their own `status` field)

**Doc-only, no code change.** `REQ-MDIO-001` through `REQ-MDIO-019` -- every MDIO requirement covering the module's already-implemented, already-tested address-validation, burst/word packing, and ACF encode/decode primitives (`rcp_ep_mdio_addr_valid()`, `rcp_ep_mdio_word_encode()`/`_decode()`, `rcp_ep_mdio_pack_words()`, `rcp_ep_mdio_encode_read_request()`/`_decode_read_request()`, etc.) -- had never had a `status` field added to their `.fusa-reqs.json` entry at all, the same class of gap already fixed for 9 other entries in the 2026-08-12 close-out pass (PRs #332/#333). Each function referenced by these 19 entries was individually confirmed present in `src/ep_mdio.c` and covered by `tests/test_ep_mdio.c` before flipping its entry to `status: "implemented"`.

Purely additive JSON edit (one new key per entry, no reordering, no entries added or removed) -- confirmed via `git diff --stat` (19 insertions) and an A/B `cfusa check` comparison (`git stash`) showing byte-identical output before/after, since this tool's lint/analyze/cyber findings are code-derived and cannot be affected by a JSON-only change. `cfusa trace`: 1024/1024 requirements traced and tested (the c-FuSa#99 1024-entry cap; all 19 edited entries sit at array indices 698-716, well within the tool's visible window, so none of them are silently dropped by that bug). Full rebuild + 65/65 tests (unaffected, as expected for a docs-only change).

### v0.290.0 -- 2026-08-12 (issue #201/#334 dedicated investigation: MDIO `mdio_mode` wire field, `REQ-MDIO-021`)

**`REQ-MDIO-021` flips `not-implemented` -> `partial`.**

`rcp_ep_mdio_encode_read_request()`/`_encode_write_request()`/`_decode_read_request()`/`_decode_write_request()` now place a real, wire-encoded `mdio_mode` octet as the leading byte of every request payload, before the pre-existing `clause`/`prtad`/`devad`/`regad`(/`word_count`) address prefix -- closing this requirement's own literal "no `mdio_mode` field at all" complaint. `rcp_ep_mdio_mode_for_word_count()` derives `MMD_SINGLE`/`MMD_MULTI` from `word_count` (1 vs >1), the same distinction the module already made before this fix, now genuinely present on the wire too.

**Two documented assumptions underlie this fix, both investigated against the primary source and confirmed on the rendered PDF page image (not just the text extraction) before implementation:**

1. TC18 §13.7.13.3 Table 60's own 2-bit `mdio_mode` value list is genuinely broken in the specification itself: `01b` is assigned to both "MMD, single word access" and "MMD, multiple byte access," and `00b` is never assigned to anything. This module assigns `00b` to `MMD_SINGLE` -- the only reading that gives the field's own natural `00`/`01`/`10`/`11` sequence four distinct meanings instead of three.
2. A second, larger gap surfaced designing a conformant encoding: neither Figure 43 nor Table 60 gives `mdio_address` an explicit bit width, and "MMS" (Memory Mapped Space) is plausibly the OPEN Alliance 10BASE-T1x SPI protocol's own distinct addressing concept, not IEEE 802.3 Clause 22/45 at all -- a **third addressing scheme** with no verified primary-source basis to design a wire layout for. This fix is deliberately scoped to the one addressing family c-RCP already correctly implements (MMD, i.e. both Clause-22 and Clause-45 addressing, `rcp_ep_mdio_addr_t`, unchanged). `rcp_ep_mdio_decode_read_request()`/`_decode_write_request()` recognize an incoming MMS-mode request (`RCP_EP_MDIO_MODE_MMS_SINGLE`/`_MULTI`, `rcp_ep_mdio_mode_is_unsupported_mms()`) and reject it with the new `RCP_EP_MDIO_ERR_UNSUPPORTED_MMS` rather than silently misreading its address field as if it were MMD-shaped. Status flips to `partial`, not `implemented`, for exactly this reason.

`REQ-MDIO-022` (16 vs 32-bit data width for MMS0/MMS1) stays `not-implemented`, now precisely (not just generically) blocked: it's unreachable without MMS addressing existing at all, which this fix deliberately does not add.

**Citation-drift fix, same lineage as issue #341**: the catalog's own "Figure 42"/"Table 57" citations were stale -- RC5's own renumbered figure/table are Figure 43/Table 60.

**Both findings (the `mdio_mode` defect and the missing `mdio_address` width/burst-count field) were added to the canonical spec-defects report** (`TC18_spec_defects_report.md`, items 25/26/55, plus the `_quadruple_checked.md` review copy) for OPEN Alliance to confirm or correct -- this fix's own two documented assumptions are exactly the two open questions flagged there.

Rewrote the pre-existing gap-pinning test (`test_mdio_request_prefix_carries_no_two_bit_mode_field` → `test_mdio_request_prefix_now_carries_a_two_bit_mode_field`, asserting the fix, plus a new `test_mdio_decode_rejects_mms_mode_fails_closed`) and added 9 new dedicated unit tests in `test_ep_mdio.c` covering `mdio_mode` derivation, encoding, and MMS rejection on both request paths.

Mutation-tested 4 ways (MMS rejection removed from each decode path; `mode_for_word_count()` broken; `mode_is_unsupported_mms()` broken) -- all four caught cleanly. 65/65 both trees (native + ASan/UBSan, full suite given this touches real wire (de)serialization). `cfusa check`: 0 errors both trees. `cfusa trace --req-coverage 100`/`--sec-tested 100` (CI's own separate invocations): both 100%.

### v0.289.0 -- 2026-08-12 (issue #256 Group I dedicated investigation: CAN EP_func register block, `REQ-CANEP-028` -- ASIL-B)

**`REQ-CANEP-028` flips `not-implemented` -> `implemented`, ASIL-B.**

New `rcp_ep_can_render_registers()`/`rcp_ep_can_apply_reconfig()` (`ep_can.h`/`ep_can.c`) model TC18 §13.7.11.2 Table 56's own CAN functional-configuration descriptor, clock and status registers: `can_ep_len` (0x0000, fixed at the block's length), the reserved octet at 0x0001, `can_base_clk` (0x0004, always renders 0 -- no real clock source modelled, matching every other endpoint type's own honesty on this point), `can_ep_status` (0x0006, new `ep_status` field), the 32-bit CAN EP status at 0x001C (new `status` field), and the 32-bit FIFO status at 0x0020 (new `fifo_status` field) -- reachable via the generic §12.7.1 evt[2:0]==111b mechanism. Bus-off, error-passive, and FIFO-overflow conditions are now observable and settable through this endpoint.

**Deliberately scoped to end at 0x0024, before `REQ-CANEP-029`'s own already-documented address collision** in the acceptance-filter region (filters 3 and 4 both print at 0x002C on both the baseline and newest PDF revisions) -- this block does not need that collision resolved, since it lies entirely outside the closed span, matching the same "don't let one unresolved sub-range block an otherwise-tractable register block" precedent `ep_wakeup.h`'s own dedicated investigation (task #95) already established.

**The 0x0008-0x001B span (`can_clk_divider`, two reserved regions, the three "CAN bit time register" fields, and TDCC) is deliberately left read-only, rendering 0 for now**: an earlier investigation (issue #256 Group I) already found Table 56 gives those 32-bit registers no sub-field bit-layout in the specification text, so converting this module's own `rcp_ep_can_bit_timing_t` to/from their wire representation is not derivable without inventing an unverified bit-packing scheme -- this fix does not force that decision, and treats a write to that span the same fail-safe way a too-short or unrecognized write is already handled elsewhere: never silently accepted-then-discarded, visibly rejected for exactly that octet range while the rest of the write still applies.

**Citation-drift fix, same lineage as issue #341**: the catalog's own "Table 53" citation was stale -- RC5's own renumbered table for this content is Table 56, confirmed directly against the primary source (RC1's Table 53 → RC5's Table 56, matching the established +3 shift for tables in this numeric range).

Rewrote the pre-existing gap-pinning test (`test_can_block_lacks_registers_and_receive_filter_table` → split into `test_can_register_block_round_trips_ep_status_and_status_fields`, asserting the fix, and `test_can_block_lacks_receive_filter_table`, keeping the still-open receive-filter-table deviation pin, `REQ-CANEP-029`'s own scope).

Mutation-tested 3 ways: the undecomposed-span read-only guard removed (confirmed a genuine **equivalent mutant** -- `parse_can_registers()` has no backing field for that span at all, so nothing persists the write regardless of whether the guard rejects it; the guard stays in for defensive/self-documentation value, not because it's independently observable here), the out-of-range boundary weakened (`>` → `>=`, caught cleanly), and `ep_status` rendering broken (caught cleanly). 65/65 both trees (native + ASan/UBSan, full suite given this touches real wire (de)serialization). `cfusa check`: 0 errors both trees. `cfusa trace --req-coverage 100`/`--sec-tested 100` (CI's own separate invocations): both 100%.

### v0.288.0 -- 2026-08-12 (issue #201 batch: `REQ-SEQ-012`, disabled-sequencer guard -- ASIL-B)

**`REQ-SEQ-012` flips `partial` -> `implemented`, ASIL-B.**

`rcp_compound_start_condition_met()` and `rcp_compound_advance_guard()` (`request_compound.h`/`.c`) both now check a sequencer's current state against 0 explicitly, before either function's own ordinary `start_state` comparison -- TC18 §12.7.10 Table 28: a sequencer manually written to 0 is DISABLED, and no compound or compound-wait step conditioned on it may become executable, nor may any advance move it out of 0 until it is explicitly rewritten to a nonzero state. This closes a real gap in `start_condition_met()`'s own "start in any state" wildcard (`start_state==0`), which previously treated a disabled sequencer as satisfying every step unconditionally -- the exact case this fix exists to close.

`rcp_e2e_endpoint_in_safe_state()` (`REQ-E2E-018`) also now fails closed on a disabled (state==0) sequencer, rather than reporting "in safe state" if `rx_safe_sequencer_state` happened to also be (mis)configured to 0 -- a disabled sequencer conveys no application-state information at all, so it can never itself satisfy a safe-state check.

**Citation-drift fix, same lineage as issue #341**: `.fusa-reqs.json`'s own "Table 25" citations (`REQ-SEQ-010`, `REQ-SEQ-012`, `REQ-SEQ-013`) and several matching code/test comments were stale -- RC1's own Table 25 (SEQUENCER_config) is RC5's own Table 28, confirmed directly against the primary source. Corrected everywhere it's cited for this table; `REQ-SEQ-013`'s own still-open access-control gap (a real, separate security finding tracked under issue #335, not attempted in this batch) keeps its `not-implemented` status, only its citation changed.

Rewrote the pre-existing gap-pinning test into `test_sequencer_zero_state_disables_start_condition_and_advance` (both the wildcard and non-wildcard disabled cases, plus re-enabling via an explicit nonzero rewrite), split the ownership/regmap-wiring half (`REQ-SEQ-013`'s own still-open deviation) into its own unchanged test. Added dedicated unit tests: `test_advance_guard_false_when_sequencer_disabled` (`test_request_compound.c`) and `test_endpoint_in_safe_state_fails_closed_when_sequencer_disabled` (`test_e2e.c`).

Mutation-tested 3 ways (the disabled-check dropped from each of the three fixed functions in turn) -- all three caught cleanly. 65/65 both trees (native + ASan/UBSan, full suite given this touches ASIL-B `request_compound.c`/`e2e.c`). `cfusa check`: 0 errors both trees. `cfusa trace --req-coverage 100`/`--sec-tested 100` (CI's own separate invocations): both 100%.

**Housekeeping**: a byte-identical stray duplicate `tests/test_request_compound 2.c` (macOS filesystem-sync artifact, missing this batch's own last `RUN_TEST` line) found and removed before committing -- same class as this session's earlier `src/ep_spi 2.c`/`src/ep_pwm 2.c` incidents.

### v0.287.0 -- 2026-08-12 (issue #201 batch: `REQ-SRV-018`, RC Server PTP trigger signals)

**`REQ-SRV-018` flips `not-implemented` -> `partial`.**

New `rcp_server_gptp_trigger_evaluate()`/`rcp_server_gptp_trigger_state_t` (`server.h`/`server.c`) derives TC18 §13.7.1.3 Table 37's own trigger signal 0/1 from a genuine gPTP lock transition -- signal 0 (`RCP_SERVER_GPTP_TRIGGER_ESTABLISHED`) on a false→true edge, signal 1 (`RCP_SERVER_GPTP_TRIGGER_LOST`) on true→false (signal 2 stays unimplemented -- it's "t.b.d." in the specification itself, not a local gap). A caller-owned tracker holds the previously observed lock state, the same architecture already established by `rcp_ep_adc_trigger_state_t` (`ep_adc.h`) and `rcp_e2e_seq_tracker_t`/`rcp_e2e_stream_fault_tracker_t` (`e2e.h`). Composed by hand with the pre-existing `rcp_server_endpoint_notify_trigger()`, the derived signal correctly arms a stored triggered request.

**Deliberately still `partial`, not `implemented`**: nothing in this library's own dispatch loop calls `evaluate()`+`notify_trigger()` together automatically on every tick yet -- the same "primitive complete, dispatch wiring deferred" disposition already established for `REQ-GPIO-033`/`REQ-ADC-031`/`REQ-SRV-016`.

**Citation-drift fix, same lineage as issue #341**: the catalog's own "Table 34" citation was stale -- RC5's own renumbered table is Table 37 (§13.7.1.3), confirmed directly against the primary source. Corrected alongside the fix.

Rewrote the pre-existing gap-pinning test (`test_gptp_lock_transition_issues_no_trigger_signal` → `test_gptp_trigger_evaluate_derives_signal_and_composes_with_notify`) to assert the fixed behavior, plus a new dedicated test for the `has_previous` first-observation guard specifically (the original test's own first observation happened to already agree with the tracker's zero-initialized default, which would have silently hidden a missing guard).

Mutation-tested 2 ways (`has_previous` guard dropped; `ESTABLISHED`/`LOST` signal values swapped) -- the first mutation was NOT caught by the original test alone, a genuine test-coverage gap (not an equivalent mutant): the new discriminating test was added specifically to close it, then both mutations were caught cleanly on the re-run. 65/65 both trees (native + ASan/UBSan, full suite given this touches core `server.c`). `cfusa check`: 0 errors both trees. `cfusa trace --req-coverage 100`/`--sec-tested 100` (run as CI's own separate invocations, not a combined call): both 100%.

### v0.286.0 -- 2026-08-12 (issue #336 batch: `REQ-ACF-032` shared GBB request_type peek + `REQ-SRV-015` GBB half)

**New shared primitive: `rcp_acf_peek_gbb_request_type()` (`REQ-ACF-032`, `acf.h`/`acf.c`).**

Every conditional-request module (`request_compound.h`, `request_triggered.h`, `request_chained.h`, `request_timed.h`) independently repurposes ACF_GBB's own 8-byte `message_timestamp` region (frame offset 8, §11.2.2) identically: octet 0 of it carries a 1-byte `request_type` opcode, common to every conditional kind. Nothing in this codebase previously let a caller classify a GBB frame's own request kind without already knowing, and calling into, one specific conditional-request module's own decoder -- a real, recurring gap that had blocked more than one fix needing only to classify a GBB frame, not fully decode it. `rcp_acf_peek_gbb_request_type()` reads `frame[8]` after confirming `frame_len >= 9` and the header's own `acf_msg_type` is `RCP_ACF_MSG_TYPE_GBB`; returns `false` for an ABB frame (no `request_type` concept exists on that wire shape) or a too-short frame, `*out_request_type` left unchanged.

**`REQ-SRV-015` flips `partial` -> `implemented`: the GBB half is now closed.**

`rcp_server_endpoint_submit()`'s existing ABB-only `evt[2:0]==111b` configuration-write fast path (v0.285.0) now also covers GBB frames, using the new primitive: a GBB frame's own `request_type` is peeked, and Compound Wait is excluded by name via the pre-existing `rcp_request_type_is_compound_wait()` predicate (`request_compound.h`) -- Compound Wait's own `evt[2:0]` means an 8-way comparison-operator selector under §13.5.1, never a configuration-write signal, even when its bit pattern happens to equal `111b`. Every other GBB `request_type` (Compound, Triggered, Chained, Timed) is treated the same as an ABB request. A GBB frame whose `request_type` cannot be peeked at all (too short, or genuinely not one of the six currently-defined values) is conservatively queued, the same fail-safe default this function already applies to a too-short ABB frame. Compound Wait itself remains queued unconditionally -- a deliberate, permanent carve-out per §13.5.1, not a remaining gap. `REQ-SRV-016`'s own cross-reference updated to reflect the requirement being fully (not partially) resolved.

Root-caused and closed a memory-flagged "3 items blocked by the same conditional-request decode gap" finding from an earlier batch this session: on investigation, only `REQ-SRV-015`'s GBB half was actually unblocked by this primitive. `REQ-PWM-057`'s phase-shift rule remains blocked by TC18's own live, unresolved `request_type` wire-format collision (0x0F/0x8F and 0x0E/0x8E both currently double-assigned across Trigger/Compound/Triggered/Compare in the spec's own text) -- not a decode-infrastructure gap, and not fixable locally without a spec ruling. `REQ-ADC-037` was a mis-association: it is an unrelated response-cadence/orchestration gap, not a request-kind-classification problem.

Split the pre-existing gap-pinning tests into `test_disabled_endpoint_executes_config_requests_immediately` (now asserts both the ABB and the new GBB-Compound cases execute immediately) and `test_disabled_endpoint_still_queues_operational_and_compound_wait_requests` (narrowed from "GBB requests" generically to specifically Compound Wait, the only remaining exclusion, plus a new assertion building a genuine GBB Compound Wait frame via `rcp_compound_encode_request()` and confirming it still queues). New `test_peek_gbb_request_type()` in `tests/test_acf.c` covers the primitive directly (GBB success, ABB rejection, too-short rejection).

Mutation-tested 3 ways: the new GBB `acf_msg_type` branch in `rcp_server_endpoint_submit()` (weakened to always take the ABB path -- caught), the Compound Wait exclusion (negation flipped -- caught by both new tests), and `rcp_acf_peek_gbb_request_type()`'s own length guard (`< 9u` weakened to `< 8u` -- caught). All three restored cleanly, confirmed via a clean rebuild.

65/65 both trees (native + ASan/UBSan, full suite run under both given this touches core `server.c`). `cfusa check`: 0 errors both trees; a handful of pre-existing warning/info instances shift line numbers only, no new rule categories. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100% -- though see [SoundMatt/c-FuSa#99](https://github.com/SoundMatt/c-FuSa/issues/99), a real defect found in `cfusa` itself while investigating this batch's own trace output: `cmd_trace.c`'s hardcoded `MAX_REQS=1024` silently truncates `.fusa-reqs.json` parsing past the 1024th entry (no error emitted), so this repo's own catalog (now 1069 entries) has had its tail silently unchecked by `cfusa trace` for some time; the "100%" figure is against the capped 1024, not the true total. Confirmed via direct JSON parsing that every requirement the tool called "dangling" genuinely exists in the file. Not a c-RCP catalog defect -- filed against the tool, not worked around in the tool's own source.

**This turned out to be immediate, not theoretical: it broke this PR's own CI.** `REQ-ACF-032`'s original position (alongside its topical neighbor `REQ-ACF-031`) shifted every later array entry's index by one, pushing a different requirement out of `cfusa trace`'s 1024-entry window and failing the CI gate for real: `sec-tested gate failed: 99% < required 100%`. Fixed by moving the new entry to the tail of the `.fusa-reqs.json` array -- JSON array order carries no semantic meaning (lookup is by `id`, not position), so appending avoids perturbing any existing entry's index. Verified locally before the fix-up push: both gates back to 100% (1024/1024), matching `main`'s own passing state exactly. New standing practice until c-FuSa#99 lands upstream: append new catalog entries at the array's tail, not positionally near related neighbors.

### v0.285.0 -- 2026-08-12 (issue #336 batch: `REQ-SRV-015`, disabled-endpoint config-request execution for ABB requests)

**`REQ-SRV-015` flips `not-implemented` -> `partial` (ABB/Standard requests only).**

`rcp_server_endpoint_submit()` now inspects an ABB request's own `evt[2:0]` (TC18 §12.3.1.3): `111b` -- Table 33's own universal per-row "EP_func configuration write" meaning, §12.7.1 -- is executed immediately even while the endpoint is disabled, including the write that would set `ep_enable` itself; any other `evt[2:0]` value (an operational request) is still queued, as before.

**Deliberately NOT applied to GBB (Conditional) frames, for a real reason surfaced during investigation**: a GBB frame might be a Compound Wait request, whose own `evt[2:0]` means an entirely different thing under §13.5.1 (an 8-way comparison-operator selector -- see `acf.h`'s `rcp_acf_compound_wait_match()` -- not a configuration-write signal), and `rcp_server_endpoint_submit()` has no request-kind decode (that lives in `request_compound.h`/`_triggered.h`/`_chained.h`/`_timed.h`, which it has no connection to) to tell a Compound Wait's own `evt[2:0]=111b` apart from any other conditional kind's config-write use of the same value. Misclassifying the former would execute an operational request immediately on a disabled endpoint -- exactly the bug this fix exists to close, not one to introduce. GBB frames remain queued unconditionally, the still-open remainder of this requirement.

Split the pre-existing gap-pinning test into two: a new `test_disabled_endpoint_executes_abb_config_requests_immediately` asserting the fix, and the original narrowed/renamed to `test_disabled_endpoint_still_queues_operational_and_gbb_requests`, pinning the remaining ABB-operational and GBB-unconditional deviations. `REQ-SRV-016`'s own cross-reference to this requirement's "still-open gap" updated to reflect the partial resolution.

Mutation-tested 2 ways (ABB message-type check flipped to GBB; `evt[2:0]` target value `0x07`→`0x00`) -- both caught cleanly, the second cascading into 3 other pre-existing tests as expected (confirming the fix's own correctness is load-bearing for behavior those tests already depend on).

65/65 both trees (native + ASan/UBSan, full suite run under both given this touches core `server.c`). `cfusa check`: 1 new instance of the same pre-existing `CFUSA-L004` false positive (the tool's naive name-matching flags one of `rcp_server_endpoint_admit()`'s two pre-existing calls to `rcp_server_endpoint_submit()` as "function 'submit' appears recursive" -- confirmed non-recursive by inspection, `submit()` is called only from `admit()`, never from itself). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.284.0 -- 2026-08-12 (issue #336 batch: `REQ-PWM-057`, PWM_OUT generation-state classifier)

**`REQ-PWM-057` flips `not-implemented` -> `partial` (2 of TC18 §13.7.5.3's own 4 request rules).**

New `rcp_ep_pwm_out_generation_state()` (`ep_pwm.h`/`ep_pwm.c`) classifies the endpoint's own signal-generation state purely from its `{period, active_duration}` pair -- `period == 0` stops generation; `active_duration == 0` with `period != 0` keeps the endpoint running with the output disabled while trigger signals still fire; otherwise ordinary running generation. Pure, caller-driven classifier over the existing setpoint pair -- `rcp_ep_pwm_out_apply_write()` itself is unaffected, both fields remain opaque 16-bit setpoints stored verbatim, matching every other caller-driven primitive in this codebase.

**The other 2 of the 4 rules investigated and correctly scoped out, not routine**: a trigger-configuration request's first two payload octets carrying a PHASE SHIFT instead of a period depends on the conditional-request layer's own request-kind classification (`request_compound.h`/`_triggered.h`/`_chained.h`), which this endpoint's decode path has no connection to today -- and the TC18 spec-defects report's own items 11-12 document a live, currently-unresolved `request_type` code collision in exactly that harmonization effort, making "which request even counts as a trigger configuration" itself an open spec question, not just an unwired integration. The output-pin-readback rule needs real physical IO this protocol-codec library has never modelled for any endpoint type.

Split the pre-existing `REQ-PWM-057` gap-pinning test (`test_pwm_out_request_semantics_are_verbatim_setpoints`) into two: a new `test_pwm_out_generation_state` asserting the fixed classifier, and the original narrowed to pin only the remaining phase-shift/pin-readback deviation.

Mutation-tested 2 ways (period boundary `0`->`1`; active_duration boundary `0`->`1`) -- both caught cleanly.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 net new findings (142 errors both before and after -- confirmed by direct comparison, not just count). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

**Housekeeping**: a byte-identical stray duplicate `src/ep_pwm 2.c` (macOS filesystem-sync artifact) was caught and removed before committing, same class as this session's earlier `src/ep_spi 2.c` incident.

### v0.283.0 -- 2026-08-12 (issue #336 batch: `REQ-ADC-033`, ADC sample-cadence catalog fix + stale REQ-ADC-035/036 test found and fixed)

**`REQ-ADC-033` flips `not-implemented` -> `partial`. `REQ-ADC-035`/`REQ-ADC-036`'s own test suite gains its own, separate fix. No functional code change.**

The catalogued claim that `rcp_ep_adc_functional_cfg_t` "carries neither `adc_sample_interval` nor `adc_base_clk` nor `adc_base_clk_divider`" was stale: `sample_interval`/`base_clk_divider` have existed as real config fields, wired to Table 51's own register block, since `REQ-ADC-035`/`REQ-ADC-036`'s earlier batch (2026-08-11). What remains genuinely unimplemented, and for a documented reason rather than an oversight: `adc_base_clk` itself is deliberately never modelled as a real value (always renders 0, the same "no real clock source" honesty `ep_gpio.h`'s/`ep_i2c.h`'s/`ep_lin.h`'s own `base_clk` fields already commit to), so this module has no way to convert `adc_sample_interval`'s own cycle count into real wall-clock spacing, and `rcp_ep_adc_average_interval()` still consumes caller-supplied samples with zero timing validation. Enforcing the temporal geometry would mean inventing a clock model this codebase deliberately doesn't have for *any* endpoint type -- not a routine field-wiring fix, so the requirement stays `partial`, not `implemented`.

**Found and fixed in passing, while investigating this requirement's own neighborhood**: `test_adc_block_has_no_clock_status_or_interval_registers` (`test_tc18_gaps_ep2.c`) was itself a stale gap-pinning test for `REQ-ADC-035`/`REQ-ADC-036` -- it still asserted the pre-fix struct footprint (5 octets) and a comment claiming none of Table 51's clock/status/interval registers existed, a full session-day after those two requirements were already fixed and independently, thoroughly covered by `test_ep_adc.c`'s own dedicated register-block round-trip tests. Rewrote it as `test_adc_functional_cfg_has_clock_status_and_interval_fields`, positively confirming all 6 added fields exist (14-octet footprint) rather than re-duplicating `test_ep_adc.c`'s own coverage. **8th+ occurrence of this session's own recurring stale-catalog-entry pattern**, this time on the test side rather than the `.fusa-reqs.json` side.

Updated `test_adc_inter_sample_spacing_is_unconstrained`'s own deviation-pin comment to correctly attribute the remaining gap to the deliberate no-real-clock-model architecture, not a missing field.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 new findings (same 1 pre-existing `CFUSA-L004` false positive, only line-shifted). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.282.0 -- 2026-08-12 (issue #336 batch: `REQ-ADC-032`, ADC channel/analog-input-pin binding, doc-only)

**`REQ-ADC-032` flips `partial` -> `implemented`. No functional code change.**

The catalogued claim -- "`regmap.h`'s `rcp_regmap_named_signal_t` index has no ADC entry, so an ADC endpoint's analog input cannot be bound to a hardware pin through the `hw_pin_map` table at all" -- was stale: `RCP_REGMAP_SIGNAL_ADC_IN` has existed since an earlier batch (`REQ-RMAP-044`), and `rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_ADC_IN)` returns `0`, its own Table 23 `EP_Signal_Nr`. An ADC endpoint's analog input already binds to a hardware pin via an ordinary `hw_pin_map` entry (`hw_ep_nr` = the ADC endpoint's own number, `hw_ep_pin_nr` = 0) -- the same generic, endpoint-type-agnostic mechanism every other endpoint type already uses. No ADC-specific binding code was ever needed.

**The one-channel-per-endpoint rule turns out to be structurally guaranteed, not something requiring separate validation**: TC18 Table 23 enumerates exactly one ADC-relevant signal (`ADC_IN`, `EP_Signal_Nr` 0), so `hw_ep_pin_nr` for an ADC endpoint's own binding can only ever be 0 -- there is no second channel number the addressing scheme could even express. Whether a real deployment's own `hw_pin_map` actually populates that row is caller/config-time data, the same as every other endpoint type's own pin binding, not a decode/encode gap in this library.

**7th+ occurrence of this session's own recurring stale-catalog-entry pattern** (after `REQ-UART-036`, `REQ-SPI-033`, `REQ-RMAP-033/034/037`, and others) -- caught this time by directly checking `regmap.h`'s own signal enum before assuming any code was missing, per this project's standing "verify against live code, not the catalog's own claim" discipline. Updated the pre-existing test's own comment (`test_adc_value_width_and_named_analog_input_signal`) to reflect the closed disposition; no new assertions needed since the existing ones (signal name + `EP_Signal_Nr`) already fully demonstrate the fixed claim.

65/65 both trees, unchanged (no functional code touched). `cfusa check`: 0 new findings. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.281.0 -- 2026-08-12 (issue #336 batch: `REQ-UART-033`, UART read-completion arbitration)

**`REQ-UART-033` flips `partial` -> `implemented`.**

New `rcp_ep_uart_read_completion_decision()` (`ep_uart.h`/`ep_uart.c`) arbitrates TC18 §13.7.8.1's own three UART read-completion triggers -- read_size satisfied, uart_timeout expired, or (when read_size exceeds `uart_rx_fifo_size`) the fifo has filled to capacity, requiring a fragmented response. This module already supplied all three ingredients (`read_size` in the ACF header, `cfg->uart_timeout_ms`, and the pre-existing `rcp_ep_uart_encode_read_response_fragmented()`) but never arbitrated between them -- two conforming c-RCP-based servers could answer identical read requests with materially different response cadence and fragmentation.

Pure, caller-driven computation over explicit counters (`bytes_available`, `read_size`, `elapsed_ms`, `uart_timeout_ms`, `rx_fifo_size`) -- this module still owns no real FIFO or clock, matching every other caller-driven primitive in this codebase (`rcp_ep_spi_transfer_length()`, `rcp_ep_adc_trigger_evaluate()`, etc.) rather than inventing timer/buffer state a protocol-codec library has no business owning.

**Investigated and correctly scoped out of this batch**: `REQ-UART-032` (RX FIFO overflow flagging) needs a bit position within `uart_ep_status` that TC18 never defines (`"Overflow is flagged in the UART EP status register"` with no bit-level breakdown given anywhere) -- the same spec-silence pattern already found in several other endpoint types' own `_ep_status` registers (see the TC18 spec-defects report's item 23). Implementing it would mean inventing a bit position unilaterally; left open, and the pre-existing deviation-pin test narrowed to cover only this remaining half.

Split the pre-existing combined `REQ-UART-032`/`REQ-UART-033` gap-pinning test (`test_uart_rx_fifo_size_bounds_nothing_at_all`) into two -- the original narrowed to pin only the `-032` deviation, plus a new dedicated `test_uart_read_completion_decision` covering all three triggers and the not-yet-complete case.

Mutation-tested 3 ways (each boundary `>=`/`>` in turn: fragmentation-fifo-full, read_size-satisfied, timeout-expired) -- all 3 caught cleanly.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 new *classes* of finding -- 8 new instances of the same pre-existing `CFUSA-L004` recursion-rule false positive already carried on `main` (this time the tool's naive name-matching flags a call to a function whose name ends in "decision" as recursive; confirmed non-recursive by inspection -- single definition, no self-call). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.280.0 -- 2026-08-12 (issue #336 batch: `REQ-SPI-034`, SPI trigger output numbering)

**`REQ-SPI-034` flips `not-implemented` -> `implemented`.**

New `rcp_ep_spi_trigger_signal_number()` (`ep_spi.h`/`ep_spi.c`) computes TC18 §13.7.3.1's own Table 41 "spi trigger outputs" per-channel signal numbers: signal 0 is the whole-endpoint "SPI execution done" trigger (not modelled by this per-channel function, the same treatment as `REQ-GPIO-034`'s own signal 0), signal 1 is reserved, and signal `2+2n`/`3+2n` is CSn asserted/de-asserted, for `0 <= n < 16` narrowed to this module's own `RCP_EP_SPI_MAX_CHANNELS` (6) -- exactly the requirement's own "signals 2..13" range.

**Citation correction found and fixed alongside this fix**: the `.fusa-reqs.json` record (and this module's own gap-pinning test comment) cited "Table 38" -- confirmed directly against the current RC5 baseline PDF that Table 38 is now the unrelated RC-Server worked example (`13.7.1.4`, itself a `TABLE TO BE UPDATED` placeholder), not the trigger-outputs table. The real table is Table 41; both `.fusa-reqs.json` and the test comment now cite it correctly.

This is a pure, additive numbering computation, entirely independent of `rcp_ep_spi_trigger_t`'s own deliberately-collapsed, non-wire-rendered per-channel trigger mode (documented in `ep_spi.h`'s own file header, c-RCP-AUDIT-06/issue #256 Group C) -- that design decision, and its "no wire-format consequence" property, are unaffected by this fix.

Returns `false` (leaving the output unchanged) for `channel >= RCP_EP_SPI_MAX_CHANNELS`, `TRANSFER_DONE` (signal 0's whole-endpoint concept, no per-channel Table 41 entry), or `NONE` (names no trigger event), rather than fabricating a signal number.

New `test_spi_trigger_signal_numbering` asserts channel 0's pair (2/3), channel 1's first signal (4), channel 5's pair (12/13, this module's own highest channel), and all three `false` cases.

Mutation-tested: both the channel boundary (`>=` weakened to `>`) and the signal-number formula (the `2+` offset dropped) are independently caught by the new test.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 new *classes* of finding -- 8 new instances of the same pre-existing `CFUSA-L004` recursion-rule false positive already carried on `main` (confirmed non-recursive by inspection: the rule flags every *call site* of a newly-introduced function name, not just its definition, once the function has multiple call sites in a test file). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.279.0 -- 2026-08-12 (issue #336 batch: `REQ-GPIO-034`, GPIO trigger signal numbering)

**`REQ-GPIO-034` flips `partial` -> `implemented`.**

New `rcp_ep_gpio_trigger_signal_number()` (`ep_gpio.h`/`ep_gpio.c`) computes TC18 Table 40 (RC1)/Table 43 (RC5)'s own per-pin trigger signal numbers: signal 0 is the whole-endpoint "GPIO EP request execution done" trigger (not modelled by this per-pin function -- it names no `(pin_index, trigger)` pair at all), and for each pin IOn, signal `3n+1`/`3n+2`/`3n+3` is `ANY_CHANGE`/`RISING`/`FALLING`, running up to signal 96 for IO31's own `FALLING` entry -- confirmed directly against the current RC5 baseline PDF page 99. `rcp_ep_gpio_trigger_t`'s own ordinal values (`ANY_CHANGE=1, RISING=2, FALLING=3`) were already numbered to equal Table 43's own per-pin offset, so the implementation is exactly `3*pin_index + (uint8_t)trigger`, with no per-case arithmetic needed.

Returns `false` (leaving `*out_signal_number` unchanged) for `pin_index >= RCP_EP_GPIO_MAX_PINS` or `trigger == RCP_EP_GPIO_TRIGGER_NONE` (which names no trigger event and therefore no Table 40/43 signal number), rather than fabricating a number for either case.

New `test_gpio_trigger_signal_numbering` asserts pin 0's three signals (1/2/3), pin 1's first signal (4), pin 31's `FALLING` (96, the table's own highest entry), and both `false` cases.

Mutation-tested: both the pin-index boundary (`>=` weakened to `>`) and the signal-number formula (trigger offset dropped) are independently caught by the new test.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 new findings (128 pre-existing baseline findings on unmodified `main`, all `CFUSA-L004` recursion-rule false positives unrelated to this change, confirmed via `git stash` A/B comparison). `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.278.0 -- 2026-08-12 (issue #334 batch: `REQ-SEQ-014`, sequencer_state now readable over EP0)

**`REQ-SEQ-014` flips `partial` -> `implemented`.**

`rcp_regmap_ep0_encode_read_response()` (`regmap.h`/`regmap.c`) now routes an incoming EP0 read across a **seventh** extent -- `sequencer_state`, addressed via `svr_sequencer_state_ptr` the same way the six existing pointed-to tables (HW_config/EP_ID_config/response-queue-config/request-stream-cfg/ep_generic_cfg) already are. This extent needed no dedicated `render()` step of its own: `rcp_sequencer_table_t.state` (`request_sequencer.h`) *is already* TC18's own one-octet-per-sequencer `Seq_state` wire image, so the raw bytes are passed straight through the dispatcher. `regmap.h` deliberately takes a bare `uint8_t` pointer and count rather than `request_sequencer.h`'s own struct type, preserving that module's documented no-cross-dependency layering. A caller with no sequencer table at all (`rcp_sequencer_table_unsupported()`) passes `NULL`/`0`, matching every other optional extent's own convention -- the dispatcher correctly falls through to its existing unknown-extent `RCP_ERROR_EP_NOT_FOUND` case rather than dereferencing a null pointer.

The other half of this requirement (`svr_sequencers_max` synced with the live table's count) was already done via `mock.c`'s pre-existing `rcp_mock_server_set_sequencer_count()` -- the `.fusa-reqs.json` text describing it as unwired was stale and is corrected alongside this fix.

Write access to `sequencer_state` (a client setting `Seq_state`, e.g. to 0 for `REQ-SEQ-012`'s own disable rule) remains a separate, still-open gap -- this closes the read path only, matching the requirement's own original scope.

Renamed `test_ep0_read_dispatcher_routes_all_six_extents_and_unknown_addresses` -> `..._all_seven_extents_...`, added two new sub-cases (real sequencer-state read, and the `NULL`/unsupported-table fallback).

Mutation-tested: the new extent's own boundary check (`>=` weakened to `>`) is caught by the new test.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.277.0 -- 2026-08-12 (c-RCP-AUDIT-10 doc-only batch: `Table 19`/`Table 21` disambiguated -- HW_config vs. signal-enumeration, issue #341)

**Documentation-only correction, no functional code change.** Genuinely intricate slice of issue #341: `§12.7.6` ("HW pin mapping configuration") contains three consecutive RC1 tables -- 19 (HW_config), 20 (IO-pin properties), 21 (Enumeration of signals) -- that all shift +2 to RC5 21/22/23. `regmap.h` used the raw number `19` for HW_config in 5 spots and the raw number `21` for signal-enumeration content in 9 spots, **while simultaneously already using the correct RC5 number `21` for HW_config in 5 other spots** -- meaning `Table 21` alone was ambiguous between two different real tables depending on which paragraph you were reading, sometimes within the same comment block.

Resolved by individual content verification, not number pattern-matching: every citation was read and classified as HW_config (svr_hw_cfg_ptr, hw_ep_nr/hw_ep_pin_nr/hw_pin_type, pin-mapping addressing) or signal-enumeration (EP_Signal_Nr, `RCP_REGMAP_SIGNAL_*` names/order) before touching it. `Table 19` -> `Table 21` (5 in `regmap.h`, 5 in `.fusa-reqs.json` -- one of which required repairing a redundant `Table 21/21` this batch's own intermediate step briefly introduced, caught by the standing `json.load()` validation step and fixed before commit). `Table 21` -> `Table 23` (9 in `regmap.h`, 4 in `.fusa-reqs.json`), leaving the 12 already-correct `Table 21` HW_config citations untouched.

**Self-caught methodology bug, again**: the same BSD `sed`-vs-`\b` incompatibility from earlier in this citation-drift lineage silently no-opped the first `Table 19`->`21` attempt on `regmap.h` -- caught immediately by re-grepping for the target string post-edit rather than trusting the command's own silent success, matching this project's own standing "verify, don't assume" discipline.

Deliberately still open, tracked in issue #341: the `hw_pin_type` citation (`config.c`/`regmap.h` line ~1409) says "§12.7.6 Table 20" -- wrong under both RC1 and RC5 numbering (RC1's own IO-pin-properties table was 20, shifting to RC5 22; this citation matches neither), needs its own dedicated investigation, not a shift.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.276.0 -- 2026-08-12 (c-RCP-AUDIT-09 doc-only fix: 11 line-wrapped table citations missed by PRs #342/#343)

**Documentation-only correction, no functional code change.** Self-caught methodology bug: the `sed`-based citation fixes in PRs #342 (v0.274.0) and #343 (v0.275.0) operated line-by-line, so any occurrence where a C block comment wrapped `Table` onto one line and the number onto the next (e.g. `/* ... Table\n * 18, absolute address ...`) was invisible to the substitution and left stale.

A script joining each consecutive line pair and re-scanning for the same four number pairs found 11 such misses, all in `regmap.h`/`acf.h`: 6 more `Table 18`→`20`, 3 more `Table 22`→`24`, 1 more `Table 30`→`33`, 1 more `Table 27`→`30`. Fixed precisely (only the digit token replaced, comment formatting otherwise untouched) and verified via the same joined-line re-scan -- two further line-wrapped hits found (`ep_uart.h` "Table\n48", `ep_can.h` "Table\n54") were confirmed to already be correct RC5 numbers and left alone.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.275.0 -- 2026-08-12 (c-RCP-AUDIT-08 doc-only batch: `Table 18`/`Table 22` citation drift corrected -- highest-value slice of issue #341)

**Documentation-only correction, no functional code change.** First batch of issue #341's broader table-number census -- the two highest-count stale citations, both confirmed via direct content cross-check (not just number pattern-matching) against both spec revisions.

- **RC1 Table 18 "RC Server configuration static part" (§12.7.5) -> RC5 Table 20.** 62 occurrences (`regmap.h`, `respqueue.h`, `lifecycle.h`, `cli.c`, `regmap.c`, `config.c`, `lifecycle.c`, `.fusa-reqs.json`). Content-verified: RC5's own Table 20 (page 60-61) matches field-for-field against every citation's own described content (`svr_io_pin_count` at 0x0018, `svr_hw_cfg_ptr`, `svr_request_stream_cfg_capacity`, `svr_ep_generic_cfg_ptr`, etc.) -- this is the RC-Server's own general register map, referenced by nearly every `REQ-RMAP-0*`/`REQ-LIFECYCLE-0*` requirement.
- **RC1 Table 22 "Request stream configuration" (§12.7.7) -> RC5 Table 24.** 42 occurrences (`regmap.h`, `lifecycle.h`, `mock.h`, `e2e.h`, `server.c`, `lifecycle.c`, `mock.c`, `.fusa-reqs.json`). Content-verified against `rx_enforce_crc`/`rx_enforce_sequence`/`rx_enforce_watchdog`/`rx_ovrflw_safestate_enable`/`rx_wd_action` -- every citation's own described field lives in this exact table on both revisions.

**A genuinely mixed, inconsistent state found and deliberately left alone**: `regmap.h`'s own `request-stream-cfg` wire-codec comment block (added by issue #306, a more recent and more careful pass) already correctly cited `Table 24` for this exact table in one paragraph, while the immediately preceding paragraph of the SAME comment block still said `Table 22` -- confirms both numbers can appear for the identical real-world table even within one contiguous comment, and confirms this fix's own target (all remaining `Table 22`) needed correcting without disturbing the pre-existing correct `Table 24`. Separately, `respqueue.h`'s own `Table 24` citations (`STREAM_UID`, `max_avtpdu_size`, `queue_size`, `flush_time_us`) are for an entirely DIFFERENT table (RC1's own Table 24 "Responder QUEUE_config" -> RC5 Table 27) -- confirmed stale but **deliberately not touched in this batch**, since correcting it means introducing new `Table 27` text into a codebase that doesn't yet consistently distinguish it from the `Table 24` this batch just finished fixing; tracked as its own follow-up item in issue #341's own text. A separate, already-broken `Table 20` citation for `hw_pin_type` (`config.c`/`regmap.h`, §12.7.6, should be `Table 21`) was also found and also deliberately left alone -- same reasoning, same tracking issue.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.274.0 -- 2026-08-12 (c-RCP-AUDIT-07 doc-only batch: `Table 30`/`Table 27` citation drift corrected, issue #339)

**Documentation-only correction, no functional code change.** Every source citation of `Table 30` and `Table 27` across the codebase corrected to the current TC18 0.5.1_RC5 table numbers.

Confirmed by direct comparison of the RC1 extraction (`TC18.txt`) against a fresh RC5 `pdftotext -layout` extraction: RC5 uniformly renumbers tables from RC1's Table 24 onward by +3 (one new table was inserted earlier in the document, shifting everything after it -- the same shift already correctly applied to the seven §13.7 endpoint-table renumberings during the earlier "TC18 spec rebaseline to 0.5.1_RC5" pass, task #96). Two tables cited pervasively in code-comment prose, rather than scoped to a single endpoint's own functional-config table, were missed by that pass:

- **RC1 Table 30 "EP specific usage of evt-field" (§13.5) -> RC5 Table 33.** 52 occurrences across `acf.h` and every `ep_*.h`/`ep_*.c` file whose evt[2:0] handling references this table -- `rcp_acf_evt_row2_is_plain()`'s own doc comment, every row-2 endpoint's plain-request evt check, SPI's channel-selection row, GPIO/PWM_OUT's write-semantics row.
- **RC1 Table 27 "Error codes in responses" (§12.9.6) -> RC5 Table 30.** 14 occurrences across `discovery.h`, `regmap.h`, `acf.h`, `mock.h`, `server.h`, `server.c`, `mock.c` -- every reference to the 17-code wire error enumeration (`UNSUPPORTED_CMD` through `CHAIN_ERROR`).

**Ordering hazard resolved correctly**: `Table 27`'s NEW number (30) is exactly `Table 30`'s OLD number, so a single blind find-replace in either direction would have corrupted the result. Did the 52 `Table 30`->`Table 33` replacements first (verified none referred to anything but the evt-field table -- confirmed via full-context grep before touching any file), then the 14 `Table 27`->`Table 30` replacements. `.fusa-reqs.json` citations for both got the same two-phase correction (93 `Table 33` + 9 `Table 30` after, both counts independently verified against the pre-fix totals).

**Scope note**: a broader table-number census across the whole codebase (`Table 18`, `22`, `19`, `23`, `24`, `25`, `26`, `28`, `31`, `32`, ...) turned up hundreds more citations, some already RC5-correct (from earlier per-endpoint sweeps), some still RC1-stale -- a mixed, inconsistent state that needs per-citation context verification, not a blind number remap (a naive shift would double-correct the already-fixed ones). Deliberately NOT attempted in this batch; tracked as a much larger follow-up investigation.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.273.0 -- 2026-08-12 (issue #337 batch: `REQ-ACF-018`/`REQ-ACF-021` -- read_size/segment_num classifier + request fixed-value enforcement)

**Both `REQ-ACF-018` and `REQ-ACF-021` flip `partial` -> `implemented`.**

- **`REQ-ACF-018`**: new `rcp_acf_read_size_or_segment_num_kind()` (acf.h/acf.c) classifies byte_message_info's 12-bit `read_size_or_segment_num` field as `RCP_ACF_RSS_READ_SIZE` (op == `RCP_ACF_OP_READ`) or `RCP_ACF_RSS_SEGMENT_NUM` (every other op value), exposing the TC18 §11.2.1/§11.2.2.1 selection to callers instead of leaving the field an uninterpreted round-tripped slot.
- **`REQ-ACF-021`**: two new primitives close the gap at the two points that actually know whether they're building a request or a response (`rcp_acf_encode_abb()`/`_gbb()` are shared by both, so cannot enforce unconditionally): `rcp_acf_request_header_constraints_valid(hdr, cs_has_meaning)` is the pure encode-side validator (hs/rsp/err must be 0; cs must be 0 unless the caller says it carries compound-wait/chained meaning of its own); `rcp_acf_header_is_request(hdr)` is the decode-side `rsp == 0` check. **Real behavior change**: `rcp_server_endpoint_admit()` now calls the latter on every arriving frame, before any further classification, and refuses admission (`RCP_SERVER_ADMIT_REJECTED`, `RCP_ERROR_INVALID_PARAMETER`) when `rsp` is set -- TC18's own "a received message whose rsp bit is set shall not be admitted as a request" rule (§11.2.2.3) is now enforced, not just documented.

Updated the pre-existing gap-pinning test (`test_acf_request_flags_round_trip_unconstrained` -> `test_acf_request_flags_round_trip_but_admission_now_rejects_rsp`) to assert the fixed admission behavior instead of pinning the old deviation; added two new tests for the read_size/segment_num classifier and the cs-exemption rule. Mutation-tested: inverting the new admission check's sense fails the updated test plus 4 further pre-existing tests that build requests via `mock.c`'s dispatch path.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100% (1024/1024 requirements traced, 512/512 functions annotated).

### v0.272.0 -- 2026-08-12 (issue #200 doc-only batch: `REQ-RMAP-047`/`057`/`058` closed -- register+wire scope complete, remaining text was non-normative or already out of scope)

**`REQ-RMAP-047`/`REQ-RMAP-057`/`REQ-RMAP-058` flip `partial` -> `implemented` -- `.fusa-reqs.json` text/status corrections, no code change.**

Investigated all 18 `partial` RMAP entries (issue #200) for this batch. Three close honestly at their own current state; the rest are genuinely blocked (see the "Investigated, not closeable this batch" note below) and were left untouched.

- **`REQ-RMAP-047`** (`rx_secure_channel_index`, MACsec channel selection): register storage, wire dispatch (issue #306), and write authorization (issue #308) are all already complete. The only remaining text described this library having no MACsec (802.1AE) layer to *act on* a selected channel -- an already-decided architectural boundary, not a gap: MACsec was deliberately excluded from this library's own scope (see this file's own Deprecation & Removal Log, `tls.h`/`tls.c`, v0.78.0, same "link-layer, out of scope" reasoning). Selecting and exposing the channel index is the whole of what a register-map library is responsible for.
- **`REQ-RMAP-057`** (single-RC-Client-per-endpoint) and **`REQ-RMAP-058`** (shared-`byte_bus_id` same-`ep_type`): both cite TC18 §12.7.8 language re-verified directly against TC18.txt -- "it is **recommended**..." (L2985) and "they **should** be..." (L2988) -- non-normative, not MUST/SHALL. Both already have real, tested, read-only diagnostics (`rcp_regmap_ep_id_map_has_single_client_per_ep()`, `rcp_regmap_ep_id_map_shared_bus_homogeneous()`) reporting the condition; TC18 itself defines no corrective action for either, so a diagnostic is the full, appropriate extent of support. `REQ-RMAP-058`'s own diagnostic-to-live-data wiring remains a separate, deferred integration concern (matching this session's established "primitive complete, dispatch wiring deferred" disposition, e.g. `REQ-ADC-031`/`REQ-GPIO-033`) -- noted but does not block this requirement's own closure.

**Investigated, not closeable this batch (of the remaining 15 `partial` RMAP items)**:
- `REQ-RMAP-023`/`066`/`067` are all blocked on the SAME genuine, already-documented, unresolved Table 33/36 address collision (`regmap.h`'s own file header: 0x0002/0x0003/0x0004 each assigned to two different registers on both PDF revisions) -- building a wire codec there means picking a side of an unconfirmed hypothesis, the same class of problem as MDIO/CANEP. Needs its own dedicated investigation, not a routine fix.
- `REQ-RMAP-050` (watchdog ms<->ticks wiring): the conversion functions already exist and are correct, but the codebase's own existing comment (`regmap.h`) explicitly flags that a render-time saturation direction "could itself be an unsafe choice depending on which direction is fail-safe for a given deployment, not a judgment this library should make unilaterally." Already correctly deferred pending an explicit scope decision, not forced into this batch.
- The remaining 12 (`032`/`034`/`036`/`037`/`038`/`039`/`048`/`049`/`065`/`068`) each need real subsystems or dispatch infrastructure this codebase doesn't have yet (HW_config/EP_config/Sequencer_config/Network/PHY/TimeSync/Security table storage, ack/response-stream runtime routing, bit-level register-write operations, a real scheduler) -- correctly left as-is.

No functional code change; 65/65 both trees unchanged. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.271.0 -- 2026-08-12 (issue #200 doc-only batch: 9 RMAP entries missing their own `status` field, corrected)

**`REQ-RMAP-024/025/026/027/028/029/030/031/035` gain `"status": "implemented"` -- a `.fusa-reqs.json` data-hygiene correction, not a code change.**

Found while auditing the full gap catalog for a status report: these 9 entries' own `text` already began `"IMPLEMENTED: ..."` and described real, complete, tested work from the earlier RMAP wire-dispatch lineage (issues #301/#306/#308/#310/#311), but carried no `status` key at all -- neither `"implemented"` nor anything else. Harmless to `cfusa`'s own pass/fail (a missing key doesn't fail its checks), but a genuine catalog-accuracy gap. Spot-verified all 9 against actual code (`include/rcp/regmap.h`/`src/regmap.c` field declarations) and test tags (`tests/test_tc18_gaps_regmap.c` and others) before trusting the text -- all 9 confirmed genuinely implemented and tested.

No functional code change; 65/65 both trees unchanged. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.270.0 -- 2026-08-12 (issue #201 batch: `REQ-PWM-056`, PWM_OUT duty-cycle limits now cap the requested active time)

**`rcp_ep_pwm_out_apply_write()` now takes `duty_cycle_min`/`duty_cycle_max` and clamps the resulting `active_duration` into that range -- status flips to `implemented`.**

TC18 Table 43: "Min value of PWM active in clock cycles, requests with lower values will be capped to this limit" / "Max value of PWM active in clock cycles, requests with higher values will be capped to this limit." `rcp_ep_pwm_out_functional_cfg_t` already stored `duty_cycle_min`/`duty_cycle_max` but nothing consulted them; the requested active duration was returned verbatim, outside the configured window in both directions. The cap is applied after `evt`'s own write semantics (REPLACE/OR/AND/XOR/ADD/SUB) have already computed the new value, matching Table 43's own wording -- capped, not rejected, not applied verbatim. `period` is unaffected, since Table 43 names only "PWM active."

**Real signature change to a function with real callers**: updated every call site -- 12 in `tests/test_ep_pwm.c` (passed `[0, 0xFFFF]` no-op limits, unaffected by their own existing assertions) and 5 in `tests/test_tc18_gaps_ep.c`. Split the pre-existing combined `REQ-PWM-055`/`REQ-PWM-056` gap-pinning test into two -- `REQ-PWM-055` (trigger-signal generation from the skew-delayed output, mid-pulse firing at 0% duty) remains a genuine, still-open deviation pin: it needs a real timing/signal-generation model this protocol-codec library does not have.

**Mutation-tested 3 ways, including one correctly-identified equivalent mutant**: bypassing capping entirely (caught); the min-boundary comparison reversed to the wrong direction (caught heavily, 11+2 failures); the min-boundary's own `<` vs `<=` operator (NOT caught -- investigated and confirmed a genuine equivalent mutant, same class as `REQ-SPI-036`'s own earlier finding this session: assigning `duty_cycle_min` to a value already equal to `duty_cycle_min` is a no-op, so no test of the output can discriminate the two operators there).

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.269.0 -- 2026-08-12 (issue #201 batch: `REQ-UART-034`, UART read_size widened to the ACF header's full 12-bit field)

**`rcp_ep_uart_encode_read_request()`/`_decode_read_request()`'s `read_size` is now `uint16_t`, matching the ACF header's own 12-bit `read_size_or_segment_num` field -- status flips to `implemented`.**

TC18 §13.7.8.1 explicitly contemplates a `read_size` larger than `uart_rx_fifo_size` as the third UART read-completion trigger (alongside `read_size`-satisfied and `uart_timeout` expiry), driving a fragmented response via `rcp_ep_uart_encode_read_response_fragmented()`. `read_size` was previously narrowed to `uint8_t` (0-255), on the file header's own now-corrected reasoning that this endpoint's traffic never actually needs the fragmentation mechanism because 255 bytes always fits a single AVTPDU -- that reasoning didn't survive TC18's own text: a conforming peer's request in the 256..4095 range was simply inexpressible, not merely "unreachable in real-world use." The fragmentation mechanism (retrofitted uniformly across every Phase 20 target endpoint, already exercised end-to-end in this module's own test suite) is now genuinely reachable from a request this module can itself originate.

**Real signature change to a function with real callers**: updated every call site -- `src/adapt.c`'s `RCP_ADAPT_OP_UART_READ`, plus test call sites across `tests/test_ep_uart.c` and `tests/test_tc18_gaps_ep2.c`. Rewrote the pre-existing `REQ-UART-034` gap-pinning test (`test_uart_read_size_truncates_above_one_octet` -> `test_uart_read_size_above_one_octet_round_trips`) to assert the fixed round-trip, and added a new dedicated test in `test_ep_uart.c` verifying a value above 255 (4000) round-trips exactly.

Mutation-tested (reintroducing the 8-bit truncation on decode): caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.268.0 -- 2026-08-12 (issue #201 batch: `REQ-SRV-016`, a disabled endpoint's request queuing now emits the requested acknowledge)

**New `rcp_acf_evt_requests_acknowledge()`/`rcp_acf_build_acknowledge_response()` (acf.h/acf.c) plus a new `out_ack` parameter on `rcp_server_endpoint_submit()` -- status flips to `implemented`.**

TC18 §12.3.1.3: "Nevertheless if requested an acknowledge us sent after storing the request." `evt[3]` is TC18 §13.5's own universal, endpoint-type-independent acknowledge-request bit ("evt[3] is used to request an acknowledge. I.e. evt[3]=1 requests acknowledge" -- verified directly against TC18.txt, distinct from the per-endpoint-type `evt[2:0]` meaning Table 30 assigns). `rcp_acf_evt_requests_acknowledge()` checks it; `rcp_acf_build_acknowledge_response()` builds a genuine Acknowledge (`evt = RCP_ACF_EVT_ACKNOWLEDGE`), mirroring `rcp_acf_build_error_response()`'s own established shape. `rcp_server_endpoint_submit()` now populates its new `out_ack` output parameter with that response whenever a request is queued (`ep->ep_enable` false) and its own `evt[3]` requested one -- addressed to the request's own `byte_bus_id`/`transaction_num`, left zeroed otherwise (including fail-safe when the frame is too short to even decode a header).

**Real signature change to a function with real callers**: updated every call site across `src/server.c` (`admit()`'s own two internal calls, passed `NULL` -- see scope note below), `tests/test_server.c` (8 sites), `tests/test_tc18_gaps_server.c` (3 sites).

**Deliberately scoped to `submit()` itself, not `admit()`**: `admit()`'s own signature is unchanged, and its two internal `submit()` calls pass `NULL` for `out_ack` -- the mechanism is complete and directly testable at `submit()`'s own level, but not yet propagated up through `admit()` to its own callers (`mock.c`'s real dispatch). This is a separate, not-yet-attempted integration step, matching this codebase's established disposition for primitives whose dispatch-side wiring is a distinct concern (e.g. `REQ-GPIO-033`, `REQ-ADC-031`). Also does **not** resolve `REQ-SRV-015`'s own separate, still-open gap (distinguishing a configuration request from an operational one at a disabled endpoint) -- `evt[3]` is universal, but the classification `REQ-SRV-015` needs is not, since `evt[2:0]`'s meaning is per-endpoint-type.

Split the pre-existing combined `REQ-SRV-015`/`REQ-SRV-016` gap-pinning test into two: one still pinning `REQ-SRV-015`'s deviation, one rewritten to verify `REQ-SRV-016`'s fix directly (an `evt[3]`-set request produces a decodable Acknowledge with the right `byte_bus_id`/`transaction_num`; an `evt[3]`-clear request produces none).

Mutation-tested 3 ways (bypassing the acknowledge-request check entirely, bypassing the ack-production block entirely, and always populating `*out_ack` regardless of queuing/`evt[3]`): all 3 caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.267.0 -- 2026-08-12 (issue #201 batch: `REQ-SPI-036`, the SPI transfer-length rule -- zero-fill/full-PICO -- now implemented)

**`rcp_ep_spi_{en,de}code_transfer_request()` now carry `read_size` through the ACF header's own `read_size_or_segment_num` field, and a new `rcp_ep_spi_transfer_length()` computes TC18 §13.7.3.3's own transfer-length rule -- status flips to `implemented`.**

TC18 §13.7.3.3: "The SPI EP shall append zeros in case the read_size is larger than the number of bytes in the byte_msg_payload. The byte_msg_payload will be presented on PICO in full, even if the read_size is less than the number of bytes in the byte_msg_payload." `rcp_ep_spi_transfer_length(tx_len, read_size)` computes `max(tx_len, read_size)`: a caller driving real SPI hardware clocks `tx_data[0..tx_len)` verbatim followed by zero octets up to the returned length when `read_size` exceeds `tx_len`, and always clocks at least the full `tx_len`-byte payload on PICO even when `read_size` is smaller (never truncated) -- POCI is captured for the same length.

This is a real signature change to two existing functions with real callers, not an additive fix: `read_size` was not previously extracted or carried at all (the header slot stayed 0 regardless of payload length). Updated every call site: `src/adapt.c`'s `RCP_ADAPT_OP_SPI_TRANSFER` (defaults an absent `rcp.spi.read_size` meta key to the payload's own length -- an unannotated request still asks for exactly what it sends back, matching `rcp_ep_spi_transfer_length()`'s own no-zero-fill/no-truncation behavior for that case), plus every test call site across `tests/test_ep_spi.c`, `tests/test_adapt.c`, and `tests/test_tc18_gaps_ep.c`.

**Mutation-testing result, including one correctly-identified equivalent mutant**: dropping `read_size` on encode (caught, 1/1/1 failures across `test_ep_spi`/`test_tc18_gaps_ep`/`test_adapt`) and bypassing the `max()` computation entirely (caught, 1/1 failures) both caught cleanly. A third mutation, loosening the boundary comparison from `>` to `>=`, was NOT caught -- investigated and confirmed a genuine equivalent mutant, not a coverage gap: at `read_size == tx_len` both branches return the identical numeric value (`read_size` and `tx_len` are equal at that exact point), so no test of the return value can discriminate the two operators there. The two directional tests (`read_size > tx_len`, `read_size < tx_len`) already fully specify the function's behavior everywhere the operator choice is observable.

Split the pre-existing combined `REQ-SPI-036`/`REQ-SPI-037` gap-pinning test into two: `test_spi_read_size_round_trips_through_transfer_request` (rewritten to the FIXED convention) and `test_spi_no_error_latch` (still pins `REQ-SPI-037`, genuinely deferred -- needs a caller-owned fault-tracker plus dispatch wiring this endpoint doesn't have yet, the same shape `REQ-E2E-021` needed before its own fix). 3 new tests in `test_ep_spi.c` verify `rcp_ep_spi_transfer_length()` directly (zero-fill direction, full-PICO direction, exact-equal boundary).

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.266.0 -- 2026-08-12 (issue #201 doc-only batch: `REQ-SPI-033` was already implemented, stale catalog entry corrected)

**`REQ-SPI-033` flips `partial` -> `implemented` -- a `.fusa-reqs.json` text correction, not a code change.**

TC18 §13.7.3.1/§13.7.3.2 give the SPI endpoint six independently pre-configured channels, selected by a request's `evt[2:0]`. `rcp_ep_spi_decode_transfer_request()` already extracts and validates the requesting channel from `evt[2:0]` (`rcp_ep_spi_channel_valid()`), returning it for a caller to index `cfg->channels[]` with -- the same evt-bits mechanism this codebase's own dedicated SPI channel-selection investigation (issue #256, task #98) independently confirmed correct against Table 26/§13.5. This entry's own catalogue text simply never caught up to the already-implemented behavior. Same stale-catalog-entry pattern found and fixed 4+ times already this session (`REQ-UART-036` and others) -- caught this time by the gap-pinning test's own comment already stating "the DEVIATION is only that the catalogue, not the code, was missing that statement."

No functional code change; 65/65 both trees unchanged. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.265.0 -- 2026-08-12 (issue #201 batch: `REQ-GPIO-033`, GPIO payload-length violation now maps to the TC18 wire error code)

**New `rcp_ep_gpio_wire_error()` (ep_gpio.h/ep_gpio.c) maps `RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN` to `RCP_ERROR_INVALID_PARAMETER` -- status flips to `implemented`, matching this codebase's established `rcp_<module>_wire_error()` convention (`rcp_e2e_wire_error()`, `REQ-WIREERR-003`).**

TC18 §13.7.4.1: "A request not having exactly four bytes is rejected and an error response with error code = INVALID_PARAMETER will be sent." c-RCP's `rcp_ep_gpio_decode_write_request()`/`_decode_request()` always enforced the four-octet length, but only ever reported the violation as the module-local `RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN` -- the numbered wire code TC18 names was never reachable from this path. The new function closes that gap: a caller building an Error Response frame (`acf.h`'s `rcp_acf_build_error_response()`) now has the TC18-conformant code available. Every other `rcp_ep_gpio_errc_t` value maps to `RCP_ERROR_NONE`, matching `rcp_e2e_wire_error()`'s own disposition for its analogous local-only framing/routing codes -- they resolve before a GPIO-specific Response frame would even be constructible.

**The requirement's other half -- "an endpoint supporting fewer than 32 pins shall map its pins onto the least-significant bits" -- turned out to already be fully conformant, no code change needed**: this module's bit-index `n` <-> pin `IOn` encoding (`rcp_ep_gpio_pin_mask()`/`_pin_get()`) is fixed regardless of how many pins a real instance physically has, so pin 0 always occupies bit 0. Added a test making this explicit rather than relying on indirect coverage.

2 tests (1 rewritten gap-pinning test, 1 new covering the local-only codes). Mutation-tested (bypassing the `BAD_PAYLOAD_LEN` mapping): caught cleanly, 1 test failure.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.264.0 -- 2026-08-12 (issue #201 batch: `REQ-LIFECYCLE-038`, RCP_CFG_INCONSISTENT's third plausibility bullet -- orphaned request streams now caught)

**New `request_stream_index` field on `rcp_lifecycle_endpoint_plausibility_t` plus a new bullet-2 cross-reference scan in `rcp_lifecycle_check_rcp_cfg()` closes `REQ-LIFECYCLE-038` -- status flips to `implemented`, ASIL-B (matching sibling bullets `REQ-LIFECYCLE-005`/`006`/`007`, which this check completes).**

TC18 §12.3.1.2's RCP_CFG_INCONSISTENT plausibility check names three bullets for the HW_CONFIGURED -> RCP_CONFIGURED transition. Bullets 1 and 3 were already implemented; bullet 2 -- "For each configured stream at least one stream_id/byte_bus_id is configured," the mirror-image completeness check that no request stream is left configured with zero endpoints actually using it -- had no counterpart at all. A request stream marked `configured = true` with zero endpoints bound to it (an orphaned, unused stream slot) passed the check exactly as if it were legitimately in use. `request_stream_index` is placed as the struct's own last field so every existing positional-initializer test call site keeps compiling unchanged, matching this codebase's established backward-compatibility convention (see e.g. `rcp_regmap_ep_id_map_entry_t`'s own field of the same name/purpose). The new scan requires, for each configured request stream `i`, at least one endpoint with `ep_used && has_stream_assoc && request_stream_index == i`.

**Mutation-testing caught a real production-code correctness gap, not just a test-coverage one**: the first draft's bullet-2 scan omitted the `ep_used` check (`ep->has_stream_assoc && ep->request_stream_index == i`). This passed every test written up to that point, because the one test exercising a bullet-1 failure (`has_stream_assoc = false`) never reached bullet 2's logic at all -- it's already rejected by bullet 1's own earlier loop. Investigating why the mutation wasn't caught surfaced the actual gap: a stale/unused endpoint slot (`ep_used = false`, skipped entirely by bullet 1's own loop, its `has_stream_assoc`/`request_stream_index` never validated by anything) could incorrectly "cover" an otherwise-orphaned stream if it happened to carry leftover `has_stream_assoc = true` and a matching index. Fixed by adding `ep->ep_used &&` to the bullet-2 condition and a new dedicated test (`test_rcp_cfg_inconsistent_an_unused_endpoint_does_not_cover_a_stream`) that isolates exactly this gate.

Rewrote the pre-existing gap-pinning test (`test_rcp_cfg_inconsistent_does_not_catch_an_orphaned_stream` -> `test_rcp_cfg_inconsistent_catches_an_orphaned_stream`, this codebase's standing convention for a fixed deviation) and added 3 new tests. 5 tests total (1 rewritten, 4 new); systematic mutation-testing across 3 mutations (bypass bullet 2 entirely; ignore `request_stream_index`; ignore `ep_used`) -- all 3 caught cleanly after the fix (3, 3, and 1 test failures respectively).

**Found in passing, not part of this fix**: `cfusa trace` reports 54 pre-existing dangling test references in `tests/test_tc18_gaps_ep2.c` (test tags citing req IDs, e.g. `REQ-UART-036`/`REQ-MDIO-020`, that no longer exist in `.fusa-reqs.json`) -- confirmed present on `main` before this batch, unrelated to this fix, and non-blocking (`cfusa trace`'s coverage metrics and exit code are unaffected). Flagged as a candidate for a future stale-catalog-entry batch, matching this session's recurring pattern of `.fusa-reqs.json` entries that fell out of sync with a later, separate fix.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.263.0 -- 2026-08-12 (issue #201 doc-only batch: `REQ-UART-036` was already implemented, stale catalog entry corrected)

**`REQ-UART-036` (UART's `uart_ep_len`/reserved/`uart_ep_status` Table 48 register rows) flips `not-implemented` -> `implemented` -- this is a `.fusa-reqs.json` text correction, not a code change.**

`REQ-UART-038`'s own earlier fix (issue #256 Group I, 2026-08-11) already added exactly this register block: `rcp_ep_uart_render_registers()`/`_apply_reconfig()` (src/ep_uart.c) serialize `uart_ep_len` (`RCP_EP_UART_REG_EP_LEN`), the reserved octet (`RCP_EP_UART_REG_RESERVED_01`), and `uart_ep_status` (`RCP_EP_UART_REG_EP_STATUS`, `rcp_ep_uart_functional_cfg_t::ep_status`), all directly TC18.txt-verified and already covered by `tests/test_ep_uart.c`'s own existing register-block tests -- this entry's own text and status simply never caught up when `REQ-UART-038` landed. Same stale-catalog-entry pattern already found and fixed 3+ times earlier this session (`REQ-RMAP-033`/`034`/`037` and others) -- caught this time by noticing `REQ-UART-037`'s own text directly contradicted `-036`'s claim ("the register block... now carries these three registers" vs. "no register-map serialization").

Added the missing `//cfusa:test REQ-UART-036` tag (the test coverage was always real; only the tag was missing) and a `//cfusa:req REQ-UART-036` tag on `rcp_ep_uart_render_registers()` itself.

65/65 both trees (unchanged, no functional edit). `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.262.0 -- 2026-08-12 (issue #201 batch: `REQ-ADC-031`, the ADC endpoint's five Table 50 trigger outputs)

**New `rcp_ep_adc_trigger_state_t`/`rcp_ep_adc_trigger_evaluate()` (ep_adc.h/ep_adc.c) model all 5 of TC18 §13.7.9.1 Table 50's ADC trigger outputs -- status flips to `implemented`.**

`rcp_ep_adc_trigger_state_t` is a small caller-owned per-endpoint tracker holding the one piece of state edge detection needs (the previously observed averaged output value), matching the same caller-owned-data architecture already established by `rcp_watchdog_keeper_t`/`rcp_e2e_seq_tracker_t`/`rcp_e2e_stream_fault_tracker_t`. `rcp_ep_adc_trigger_evaluate()` takes one newly acquired averaged value plus `adc_trigger_min`/`adc_trigger_max` (`rcp_ep_adc_functional_cfg_t`'s own existing fields) and a caller-supplied `measurement_finished` bool, returning a bitmask of whichever triggers fire: triggers 0-3 are genuinely edge-triggered (a transition relative to the tracked previous value -- matching Table 50's own "falls below"/"rises above" wording exactly, not a level comparison against the current value alone); trigger 4 has no threshold concept at all and composes independently with any of 0-3 in the same call.

**Mutation-testing found and fixed a real test-coverage gap, not just a mutation-testing formality**: the first round of 2 mutations (bypassing the `has_previous` guard; loosening one boundary comparison) caught only 1 of 2 cleanly -- a boundary off-by-one on `ABOVE_MIN` (`>` loosened to `>=`) passed all existing tests undetected, because no test exercised a value moving exactly *to* (not past) a threshold from the covering direction. Added 4 new discriminating tests (one per trigger direction) and confirmed all 4 corresponding boundary mutations are now caught cleanly.

Wiring this primitive into a real Trigger-request dispatch path (the caller-side integration TC18's own cyclic-ADC pattern ultimately needs) remains a separate, not-yet-attempted integration concern, matching the same disposition already established for this codebase's other pure caller-driven primitives before their own dispatch-side wiring.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.261.0 -- 2026-08-12 (issue #201 batch: `REQ-E2E-021`, a CRC error on an `rx_enforce_e2e` stream now actually blocks the stream)

**New `rcp_e2e_stream_fault_tracker_t` (e2e.h/e2e.c) wired into `rcp_mock_server_dispatch_e2e()`/`_dispatch_frame_e2e()` closes `REQ-E2E-021` fully (TC18 §12.7.7 Table 22: `rx_enforce_e2e`'s "stream is blocked until released" consequence) -- status flips to `implemented`, ASIL-B.**

`rcp_e2e_stream_fault_on_crc_error()`/`rcp_e2e_stream_fault_t` (the single-stream fault latch) were always correct and directly unit-tested; the gap was that nothing in the dispatch path ever called them. The new tracker is a caller-owned, keyed-by-`stream_id` wrapper holding one `rcp_e2e_stream_fault_t` per stream (a real server may have more than one), matching the same caller-owned-data architecture already established by `rcp_watchdog_keeper_t` and `rcp_e2e_seq_tracker_t`.

`dispatch_e2e()` now (1) checks `rcp_e2e_stream_fault_tracker_is_faulted()` for `stream_id` **before** plain-command-mode delegation, CRC validation, or admission -- returning a new `RCP_MOCK_DISPATCH_STREAM_FAULTED` with a real Table 27 POCI_FAILURE error response, since the block is a whole-STREAM property (checked before any single request's own outcome is even considered); and (2) records every CRC mismatch it detects via `rcp_e2e_stream_fault_tracker_on_crc_error()`, keyed to a new per-endpoint `rcp_mock_server_set_endpoint_rx_enforce_e2e()` stand-in bit -- this test double's own in-process stand-in for the real per-request-stream register bit, matching `req_crc_enable`'s own already-established stand-in pattern (TC18's real `rx_enforce_e2e` lives on a different, per-stream table this type-erased slot has no way to read generically).

Wiring is entirely opt-in via `rcp_mock_server_set_stream_fault_tracker()` -- `tracker` may be `NULL` (the default) to disable stream-fault blocking entirely, not owned by `srv`, matching `rcp_mock_server_set_watchdog_keeper()`'s own lifecycle contract.

**Deliberately out of scope, not conflated with this fix**: Table 22's OTHER `rx_enforce_e2e` consequence, "Safe state will be entered," is `REQ-E2E-045`'s own separate, still-open gap -- a cross-endpoint safe-state escalation this library's single-endpoint-scoped data model has no orchestrator for, unlike the blocking half this fix closes (which only needs to reject future requests on the SAME stream, well within `dispatch_e2e()`'s own existing scope).

4 new tests in `tests/test_e2e.c` (the pure tracker: registration/isolation, reset, capacity exhaustion honestly reported) and 3 new tests in `tests/test_tc18_gaps_e2e.c` (the real integration: blocks-then-releases, `rx_enforce_e2e=false` does not block, no-tracker-set is a no-op). Mutation-tested 3 ways (bypass the pre-dispatch block check; bypass the CRC-error recording; hardcode `rx_enforce_e2e=true` regardless of endpoint config) -- all three caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.260.0 -- 2026-08-12 (issue #201 batch: `REQ-WAKEUP-017`, WakeUp message now carries the wake-up source -- WAKEUP group fully addressed)

**New `rcp_ep_wakeup_encode_wakeup_message_with_source()`/`_decode_wakeup_message_with_source()` close `REQ-WAKEUP-017` fully (TC18 §12.4.1: the repetitive wake response must convey both a WakeUp message and the WakeUp source) -- status flips to `implemented`, and this closes the last of WAKEUP's 4 not-implemented items.**

Added as a strictly additive extension, not a modification of the pre-existing 1-byte-payload `rcp_ep_wakeup_encode_wakeup_message()`/`_decode_wakeup_message()`/`_is_wakeup_echo()` trio, which keep their own original shape and behavior entirely unchanged -- avoiding a signature-widening ripple across their own real call sites (`src/adapt.c`, `src/powerstate.c`, and several test files). The new pair's own 3-byte payload (opcode + a new `rcp_ep_wakeup_source_t` classification byte + a `source_index` byte) is forward-compatible with the plain decoder (which only ever checks `payload_len >= 1` and `payload[0]`, tolerating but not requiring the longer shape) -- confirmed directly by test, not just asserted.

Covers all 3 wake-source classes TC18 §12.4.1's own text names: a configured wake-source pin (`RCP_EP_WAKEUP_SOURCE_IO`, with `source_index` into `rcp_ep_wakeup_functional_cfg_t::sources[]`), "the dedicated wakepin" (`RCP_EP_WAKEUP_SOURCE_WAKEPIN`, named separately in that text from the configured pin table, so kept as its own distinct classification rather than folded into `_IO`), and a TC14/TC10 network wake-up request (`RCP_EP_WAKEUP_SOURCE_NETWORK`), plus `RCP_EP_WAKEUP_SOURCE_UNKNOWN` for a caller with no source information to report. TC18 defines no wire encoding for this classification (same disclaimer as SleepCMD's own response payload) -- this enum and byte layout are this module's own original design.

Mutation-tested 2 ways (remove source-byte validation entirely; loosen the length check to accept the plain 1-byte shape) -- both caught cleanly; the second mutation also surfaced that a loosened length check would silently misread ACF quadlet-padding zero bytes as a valid `RCP_EP_WAKEUP_SOURCE_UNKNOWN` classification, confirming the length check is a real safety gate, not a redundant one.

**WAKEUP group (6 items total) is now fully addressed**: `REQ-WAKEUP-017`/`-019` fully `implemented`; `REQ-WAKEUP-018`/`-020` honestly `partial` (real, documented TC18 wire-format gaps, not oversights); `REQ-WAKEUP-021`/`-022` already `partial` from an earlier session (issue #256 Group I).

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.259.0 -- 2026-08-12 (issue #201 batch: `REQ-WAKEUP-018`, WakeUp repetition-time configurability)

**New `repetition_time_us` field on `rcp_ep_wakeup_functional_cfg_t` closes the "neither discoverable nor settable" half of `REQ-WAKEUP-018` (TC18 §12.4.1: "Repetition time of the message can be configured inside the WakeUp EP").**

Zero-init default 0, discoverable and settable over this module's own in-memory API -- a caller now has a value to read/write directly, closing the specific complaint this requirement's own text raised. **Stays `partial`, not `implemented`**: TC18 §13.7.2.2 Table 36 (this endpoint's own functional-config register block, already fully mapped by `REQ-WAKEUP-021`) defines no field for a repetition interval at all, so this value has no wire-register address. The only other TC18 mention of a WakeUp timing concept is §13.7.2.1's own parenthetical "(flush_time)", naming a register on a *different* table entirely (`rcp_regmap_response_queue_cfg_t::flush_time_us`, TC18 §12.7.9 Table 24, `REQ-RMAP-064`) associated with the response queue, not this endpoint's own functional config -- reusing that field would require this module to reach into a different endpoint's own response-queue row by `ep_id`/`byte_bus_id` lookup, a real architectural decision this fix deliberately does not make unilaterally. `power.c`'s own `rcp_pwrmode_handshake_t` still counts attempts (`wakeup_attempts`/`wakeup_repeat_limit`) rather than tracking a time interval; the new field's own doc comment names it as the value a caller should consult for retry cadence, without power.h taking on a dependency back on ep_wakeup.h (preserving the file header's own established one-directional dependency rule).

`test_tc18_gaps_ep.c`'s own combined `REQ-WAKEUP-017`/`-018` deviation-pin test is split: `test_wakeup_message_has_no_source_field` keeps pinning `-017`'s still-open deviation unchanged; a new `test_wakeup_repetition_time_is_configurable_but_not_wire_reachable` asserts the new field's own conforming (partial) behavior.

Mutation-tested 1 way (non-zero init default) -- caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.258.0 -- 2026-08-12 (issue #201 batch: `REQ-WAKEUP-019`, refused sleep/standby is now a genuine error response)

**`rcp_ep_wakeup_encode_sleepcmd_response()`/`_decode_sleepcmd_response()` now signal a refused standby/sleep entry as a genuine ACF Error Response carrying `REQUEST_CANCELED`, closing `REQ-WAKEUP-019` (TC18 §12.5) fully -- status flips to `implemented`.**

Before this fix, a refused entry was encoded as the SAME positive-form SleepCMD-shaped response as a successful entry (opcode byte + a module-local `RCP_PWRMODE_ENTRY_REFUSED` result byte, `err` bit clear) -- a conformant RC Client watching for an error response never saw the refusal, and only a c-RCP peer's own decoder could interpret it at all. `rcp_ep_wakeup_encode_sleepcmd_response(..., RCP_PWRMODE_ENTRY_REFUSED, ...)` now delegates to `rcp_acf_build_error_response()`, returning a real ACF Error Response (`err` set, classifies as `RCP_ACF_RESP_ERROR`) whose single payload octet is `RCP_ERROR_REQUEST_CANCELED` -- the exact numbered wire code TC18 §12.5 requires. The `RCP_PWRMODE_ENTRY_OK` path is entirely unchanged.

`rcp_ep_wakeup_decode_sleepcmd_response()` gains a matching `hdr.err` branch: an error response carrying specifically `RCP_ERROR_REQUEST_CANCELED` decodes as `RCP_PWRMODE_ENTRY_REFUSED`; any other err code is `RCP_EP_WAKEUP_ERR_BAD_OPCODE`, not silently reinterpreted as a refusal it was never built to represent. The non-error path is unchanged, including its own pre-existing fail-safe tolerance for a non-conformant peer's old-style positive-form refusal.

The pre-existing `test_tc18_gaps_ep.c` deviation-pin test (`test_wakeup_refusal_is_positive_response_not_error`, which asserted the now-fixed OLD behavior by name) is rewritten to `test_wakeup_refusal_is_a_genuine_error_response`, asserting the conforming shape instead -- matching this file's own documented convention that a gap-pinning test failing after a fix means "rewrite it to the conforming expectation," not a regression. A second new test confirms the decode side does not misclassify an unrelated error code as a refusal. Every pre-existing round-trip caller of this pair (`test_powerstate.c`, `test_tc18_gaps_server.c`, `test_ep_wakeup.c`) needed no changes -- they only assert the round-tripped result value, which this fix preserves.

Mutation-tested 2 ways (bypass the encode-side branch; accept any err code on decode) -- both caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.257.0 -- 2026-08-12 (issue #201 batch: `REQ-WAKEUP-020`, WakeUp endpoint's fixed EP_Nr)

**New `rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id()` diagnostic and `RCP_EP_WAKEUP_ENDPOINT_NUM` constant close the "no constant, no check" half of `REQ-WAKEUP-020` (TC18 §13.7.2.1: "The WakeUp endpoint is a special endpoint and as this fixed to the endpoint nr 1").**

TC18's "endpoint nr 1" refers to `rcp_regmap_ep_id_map_entry_t::ep_id` -- EP_ID_config's own EP_Nr field (TC18 §12.7.8 Table 23) -- not `RCP_EP_WAKEUP_EP_TYPE` (this codebase's own internal ep_type tag on a different table entirely; both happening to equal 1 is coincidental). The new diagnostic is shaped identically to `REQ-RMAP-057`/`-058`'s own sibling checks for this same table's other two TC18 §12.7.8 recommendations: a caller-supplied, index-parallel `ep_types[]` array (the row itself carries no `ep_type` field) checked against a caller-supplied `target_ep_type`/`required_ep_id` pair, keeping `regmap.c` free of any dependency on a concrete endpoint-type header. `byte_bus_id` itself (every `rcp_ep_wakeup_*` entry point's own routing-address parameter) is deliberately left untouched -- TC18 §13.7.2.2 states it is "also defined via the EP_ID_map" the same way as any other endpoint, so only the fixed EP_Nr is pinned, not the wire address.

**Stays `partial`, not `implemented`**: like its two siblings, this is a read-only diagnostic, not enforcement -- nothing in `rcp_regmap_ep_id_map_apply_reconfig()` rejects a write that would violate the invariant, matching this table's own established "recommendation, not enforcement" disposition throughout.

One new test mirrors `REQ-RMAP-058`'s own dedicated test shape (correct/wrong/no-such-type/vacuous cases). Mutation-tested 2 ways (always return true; check the wrong struct field) -- both caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.256.0 -- 2026-08-12 (issue #201 batch: `REQ-WDG-010`, wiring the per-stream watchdog kick into `dispatch_e2e()`)

**`rcp_mock_server_dispatch_e2e()`/`_dispatch_frame_e2e()` now call `rcp_watchdog_keeper_kick()` for every request they receive on a stream, closing the "no production call site" half of `REQ-WDG-010` (TC18 §12.7.7: "the watchdog is reset with each request received from this RC Client").**

`rcp_watchdog_keeper_kick()` (`watchdog.h`) has always been individually correct (`REQ-WDG-003`) but, before this fix, only tests called it — a live, perfectly responsive RC Client would still overflow a real integration's watchdog on a fixed schedule, exactly as `test_tc18_gaps_server.c`'s own `test_watchdog_overflows_despite_continuous_requests()` pins for the lower-level `rcp_server_endpoint_submit()` path.

`rcp_watchdog_keeper_t` (`watchdog.h`) is architected as a thin, caller-driven satellite package operating on caller-owned data — its own file header states plainly that a caller drives `rcp_watchdog_keeper_kick()` itself and that the module sends no wire traffic and owns no transport. Direct TC18.txt verification (§12.7.7, both RC1 and RC5, "the watchdog is reset with each request received from this RC Client") confirmed the rule is about RECEIPT, not successful validation or execution: a new `rcp_mock_server_set_watchdog_keeper()` associates a caller-owned `rcp_watchdog_keeper_t*` with an `rcp_mock_server_t` (not owned by `srv` — the caller keeps its own `rcp_watchdog_keeper_new()`/`_destroy()` lifecycle, matching every other satellite-package pointer this module holds without owning), and `rcp_mock_server_dispatch_e2e()` kicks unconditionally as the very first statement in the function, before "plain command mode" delegation, CRC validation, or admission are even attempted — a request this call goes on to reject (unknown `byte_bus_id`, CRC mismatch, full queue) still means the RC Client is alive and talking on that stream.

**Deliberately scoped out**: the plain, non-E2E `rcp_mock_server_dispatch()`/`_dispatch_frame()` path has no `stream_id` parameter at all to key a kick by, and widening its own signature would ripple across every existing call site in this codebase's own test suite far beyond this fix's narrow justified scope — left as a real, separate, still-open gap, documented in `REQ-WDG-010`'s own updated `.fusa-reqs.json` text (which stays `partial`) rather than silently implied. `server.h`'s own core `rcp_server_endpoint_submit()` likewise remains entirely unkicked.

Three new tests (`tests/test_tc18_gaps_e2e.c`) mirror `test_watchdog.c`'s own `test_kick_resets_timer_prevents_overflow()` pattern against a real `rcp_watchdog_keeper_t`: repeated admitted dispatches inside the configured timeout never overflow; a single CRC-mismatch (rejected) dispatch, sandwiched between two waits each consuming most of the timeout budget, still prevents overflow — proving the kick fires on receipt even when the request is rejected, and proving it independent of the constructor's own implicit initial kick (a single-dispatch test can't tell the two apart, since `rcp_watchdog_keeper_new()` itself sets `last_kick_ms` at construction); and a no-keeper-set case confirms `dispatch_e2e()` still dispatches normally when nothing is wired.

Mutation-tested 2 ways: bypassing the kick guard entirely, and moving the kick to only the request's own success path (simulating "kick on validation" instead of "kick on receipt") — both caught cleanly. The second mutation required strengthening the rejection test above the ordering-generic form it started with, since a single post-construction dispatch call could not distinguish the two.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.255.0 -- 2026-08-12 (issue #311 batch 5: EP0 dispatcher wiring for `svr_ep_generic_cfg_ptr` -- issue #311 CLOSED)

**`rcp_regmap_ep0_decode_write_request()`/`_encode_read_response()` gain a 6th and final routing block, targeting `svr_ep_generic_cfg_ptr`'s own extent -- closing issue #311 in full, all 5 batches complete.**

The write dispatcher authorizes via `rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer)` against the row as a whole before calling `rcp_regmap_ep_generic_cfg_apply_reconfig()` (#315) — confirmed via direct primary-source verification that TC18 §13.2's own surrounding prose names no table-specific lifecycle-state override the way §12.7.6 does for HW_config (the same class of check issue #308's own HW_GENERIC finding established), so the generic FUNCTIONAL_W_STAR rule genuinely applies here. `ep_type`'s own read-only handling stays entirely inside `apply_reconfig()` itself, independent of this row-level authorization. The read dispatcher routes the identical extent to `rcp_regmap_ep_generic_cfg_render()` (#314) directly, matching every sibling extent's own established pattern — no authorization gate on the read side, matching every other extent (TC18 defines no read-side access restriction for any of these tables).

New dedicated tests added to all 3 of the existing dispatcher-level test functions: a genuine read+write round trip through the new extent plus its own routing-boundary case, and an authorization-denial case (`RCP_CONFIGURED` denies a write to `ep_generic_cfg`'s own fully-R/W* `ep_description` field, proving this is a real authorization denial distinct from `ep_type`'s own unconditional no-op). Mutation-tested 3 ways (bypassing the authorization check, loosening the write-side routing boundary, loosening the read-side routing boundary); all three caught cleanly.

**REQ-RMAP-073 through -079 all flip to `implemented`** (dispatcher routing was the shared last gap every one of them tracked); new REQ-RMAP-080 tracks the dispatcher wiring itself.

**A false-positive-adjacent lesson repeated**: `cfusa trace --sec-tested` initially reported 99% after adding REQ-RMAP-080's own `//cfusa:req` tags (in `src/regmap.c`) without the matching `//cfusa:test` tag in the test file's own top-of-file list — the same tag-coverage gap pattern hit earlier this session with REQ-RMAP-071. Fixed by adding the missing test tag.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.254.0 -- 2026-08-12 (issue #311 batch 4: `rcp_regmap_ep_generic_cfg_apply_reconfig()`, the WRITE side of ep_generic_cfg's wire codec)

**`rcp_regmap_ep_generic_cfg_apply_reconfig()` closes the write side of ep_generic_cfg's wire codec, using a genuinely different design than every sibling `apply_reconfig()` in this codebase.**

**Two design decisions, both scoped via a comment on issue #311 before implementation, not decided ad hoc:**

1. **`ep_type` (relative 0x0000) is never updated**, regardless of whether the write's own byte span covers it. TC18 §13.7.1.2 (TC18.txt L4039-4040, identical RC1/RC5) states in general terms — not scoped to the surrounding paragraph's own OR/AND/XOR bit-operation mechanism — "Writing data to read only registers has no effect and request is confirmed normally." A write touching only `ep_type` returns success, not an error. **This directly confirms REQ-RMAP-068's own long-standing "UNCONFIRMED HYPOTHESIS"** (that read-only-register writes are silently no-effect, distinct from write-prohibited → `UNAUTHORIZED_ACCESS`) — ep_generic_cfg is the first table in the codebase where that hypothesis becomes directly testable; REQ-RMAP-068's own text is updated accordingly.

2. **This function does NOT use the render()-then-patch-then-reparse-the-whole-buffer idiom** every sibling `apply_reconfig()` (HW_config/EP_ID_config/response-queue-config/request-stream-cfg) uses — that idiom is safe for them only because their own `render()` is a lossless round-trip. `rcp_regmap_ep_generic_cfg_render()` (batch 3) is **not** lossless (its own defensive `ep_delay_time` fallback and `ep_req_storage_size` clamp): reparsing a whole rendered-then-patched row would silently "launder" any already-invalid field through its own fallback/clamp, even for a row/field the write never touched — a real corruption with no basis in the actual write. Instead, each of the 5 writable fields is updated **only if the write's own byte span fully covers that field's own octet range** — a write only partially covering a multi-octet field leaves that field entirely unchanged, extending the same "do not silently corrupt what wasn't fully specified" principle from the render()-lossiness problem to ordinary partial-field writes.

**7 new tests**, including a dedicated case proving an already-invalid `ep_delay_time` in an **untouched row** survives a write to a *different* row unchanged (the motivating correctness scenario) — and a partial-field-write test that, when a boundary-check mutation was applied during testing, produced a genuine stack-buffer-overflow (caught by both a normal assertion failure and AddressSanitizer), confirming this isn't a theoretical concern.

**A false-positive security-linter finding, found and fixed**: `cfusa check` flagged two `[ERROR] CFUSA-CY009 CWE-327: weak/broken cryptographic function 'des_'` findings — both were the substring `"des_"` inside a test function's own name (`..._decodes_delay_time...`), not any cryptographic code at all. Renamed (`decodes` → `extracts`) rather than fighting the linter's own crude substring match.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.253.0 -- 2026-08-12 (issue #311 batch 3: `rcp_regmap_ep_generic_cfg_render()`, the READ side of ep_generic_cfg's wire codec)

**`rcp_regmap_ep_generic_cfg_render()` serializes ep_generic_cfg's own 12-octet-per-endpoint stride (TC18 §13.2 Table 28/31), the READ side of issue #311's remaining wire-codec/dispatcher work.**

`ep_type` (0x0000) and `ep_description`/`ep_tx_buffer_size`/`ep_rx_buffer_size` (0x0004-0x000B) serialize directly. `ep_used`/`ep_delay_time` pack into octet 0x0001 (bit 0 and bits 4:5 respectively, reserved bits left 0). `ep_req_storage_size` (0x0002-0x0003) and `ep_delay_time`'s own 2-bit encoding both go through their batch-2 boundary-conversion functions.

**`ep_delay_time`'s own conversion can fail (not every internal value is one of TC18's 4 allowed ones) — render() falls back to register value 0 (1µs) rather than erroring**, matching every sibling render() function's own established non-fallible convention (HW_config/EP_ID_config/response-queue-config/request-stream-cfg all render unconditionally). This is not a rare edge case: `rcp_regmap_ep_generic_cfg_init()`'s own zero-init default (0µs) is itself not a valid register value, so every not-yet-configured endpoint hits this fallback until something explicitly sets a valid value. `ep_req_storage_size` gets the analogous treatment — clamped down to the nearest representable word count (never rounded up) rather than failing.

**`apply_reconfig()` (write side) is deliberately NOT built in this batch**: `ep_type` (0x0000) is plain R per TC18 — the first read-only field mixed into an otherwise fully-writable (R/W*) row anywhere in this codebase's wire-codec family. Correctly rejecting-or-preserving a write that touches it needs its own dedicated design pass, not a rushed extension of this batch. EP0 dispatcher routing (both directions) is also deferred.

6 new tests (byte-offset layout, 12-octet stride across multiple entries, both fallback/clamp cases). Mutation-tested 3 ways (the delay-time fallback value, the req-storage-size clamp boundary, the bit-packing shift); all caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.252.0 -- 2026-08-11 (issue #311 batch 2: `ep_delay_time`/`ep_req_storage_size` boundary-conversion pairs)

**Two boundary-conversion function pairs close the unit/encoding mismatches issue #311's own batch 1 documented but deliberately left unfixed.**

`ep_delay_time` (TC18 §13.2 Table 28/31, 0x0001.4:5, 2 bit, R/W*, restricted to exactly {1, 10, 20, 50} µs) gets `rcp_regmap_ep_delay_time_us_to_reg()`/`_reg_to_us()` (REQ-RMAP-076). The write-side direction REJECTS any microsecond value outside the 4 allowed ones rather than rounding — a silently-substituted delay would misconfigure the endpoint's own scheduling timing, so this is treated as a real R/W* configuration-input rejection case, not a saturating-is-safe case like `rx_wd_timeout_ms`/`flush_time_us`. The read-side direction masks its own input to 2 bits and can never fail.

**Cross-check finding while verifying `ep_delay_time`'s own 4 allowed values against the primary source**: TC18's own separate prose (`request_compound`/`_triggered`/`_chained`'s own field descriptions, 4 occurrences, both RC1 and RC5 revisions) instead reads `[1µs, 20µs, 20µs, 50µs]` — a duplicated 20µs where the table's own 10µs belongs, almost certainly a copy-paste typo in TC18's own text propagated identically across all 4 sites. Table 28/31's own definitive, typed register table is treated as authoritative, not the repeated prose. Documented in both the code comment and REQ-RMAP-076's own text.

`ep_req_storage_size` (0x0002, 16 bit, R/W*, in 32-bit words on the wire) gets `rcp_regmap_ep_req_storage_size_words_to_octets()`/`_octets_to_words()` (REQ-RMAP-077). **The struct field itself is widened from `uint16_t` to `uint32_t`**: the register's own maximum representable value, 65535 words, is 262140 octets, which does not fit a 16-bit octet count — a real correctness gap independent of any wire codec, not merely a units mismatch. Confirmed zero blast radius via a direct grep of the whole codebase before widening (only the init test's own assertion needed updating, `UINT16` -> `UINT32`). The write-side conversion rejects an octet count that is not an exact multiple of 4 or whose word count would not fit the register's own 16-bit width.

Both pairs mirror the established `rcp_regmap_wd_timeout_ms_to_ticks()`/`_ticks_to_ms()` shape (`bool` return, output parameter, false leaves the output untouched). 8 new dedicated tests. Mutation-tested 2 ways (one register-value branch in `ep_delay_time_us_to_reg()`, the 16-bit bounds check in `ep_req_storage_size_octets_to_words()`); both caught cleanly.

**Still no wire codec or EP0 dispatcher routing for `svr_ep_generic_cfg_ptr`** — these are boundary-conversion primitives only, REQ-RMAP-076/077 both `partial`. Issue #311's remaining 3 steps (wire codec, dispatcher wiring, authorization) are still open.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.251.0 -- 2026-08-11 (issue #311 batch 1: `rcp_regmap_ep_generic_cfg_t` gains 3 missing content-model fields)

**`rcp_regmap_ep_generic_cfg_t` previously modeled only 4 of TC18 §13.2 Table 28/31's 8 fields.** Found while scoping the largest remaining RMAP cluster (REQ-RMAP-032/033/034/036/037/038/039) after issue #308: direct primary-source verification of both PDF revisions (RC1 pp.71-72; RC5, renumbered Table 31, pp.82-83 — identical content on both) against the current struct showed `ep_description` (0x0004, 32 bit), `ep_tx_buffer_size` (0x0008, 16 bit), and `ep_rx_buffer_size` (0x000A, 16 bit) were entirely absent.

**Fixed, content-modeling only**: all 3 fields added (REQ-RMAP-073/074/075), zero-initialized matching every other member's convention. Zero blast radius — no existing consumer of `rcp_regmap_ep_generic_cfg_t` touches these fields, and `rcp_regmap_ep_generic_cfg_init()`'s own `memset()` already zero-initializes new members for free.

**Two unit/encoding mismatches found in the SAME table, documented but deliberately NOT fixed this batch**: `ep_delay_time` is internally a free `uint32_t` microsecond value consumed as a scheduling tick unit across `request_chained.h`/`request_triggered.h`/`request_compound.h`/`server.c`, but TC18's own register is a packed 2-bit enum restricted to exactly {1, 10, 20, 50} µs — changing the internal representation would ripple through the whole scheduler subsystem, so this is deliberately left for issue #311's own next batch as a boundary-conversion pair instead (reject-on-invalid, not saturate, since this is a real R/W* configuration input). `ep_req_storage_size` has the same class of gap: internal octets vs. the register's own 32-bit-word unit.

Filed as issue #311 (GitHub) with the full table layout, both mismatches, and a suggested 5-step batch order (content model → 2 boundary-conversion pairs → wire codec → EP0 dispatcher wiring + authorization) — this PR is step 1 only. `svr_ep_generic_cfg_ptr` still has zero wire codec or dispatcher routing; REQ-RMAP-073/074/075 all stay `partial` for that reason.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.250.0 -- 2026-08-11 (`.fusa-reqs.json` staleness correction: 3 RMAP entries claimed missing storage that issues #301/#306/#308 had already built)

**Documentation-only correction, no code change.** While scoping the next RMAP batch after issue #308, direct comparison of REQ-RMAP-032 through REQ-RMAP-039's own current text against `src/regmap.c`'s actual dispatcher routing conditions found 3 entries whose text had never been revisited after the table they describe was built in a later PR:

- **REQ-RMAP-033** (`svr_hw_cfg_ptr`, 0x001A): text claimed "no real HW_config table storage anywhere yet... currently always reads 0". `svr_hw_cfg_ptr` is the exact field HW_config's own dispatcher routing block addresses (issues #301/#308) — confirmed directly in `src/regmap.c`'s routing conditions, not by name similarity alone. Flips to `implemented` (this requirement's own scope is pointer-only, no adjacent capacity register).
- **REQ-RMAP-034** (request/response stream config pointers + capacities, 0x001C-0x0021): text claimed the same for both `svr_request_stream_cfg_ptr` and `svr_response_stream_cfg_ptr` — both are now the exact fields request-stream-cfg's (issue #306) and response-queue-config's (issue #301) own dispatcher routing blocks address. Stays `partial`: the two capacity registers in this same requirement's scope are not yet cross-checked against the dispatcher's own caller-supplied count parameters, the same narrower gap REQ-RMAP-032 already tracks for `svr_io_pin_count`.
- **REQ-RMAP-037** (`svr_ep_bytebus_id_map_ptr`/`_capacity`, 0x0028-0x002A): text claimed the same for EP_ID_config's own pointer, which issue #301 batch 2 already wired. Stays `partial` for the identical capacity-cross-check reason as REQ-RMAP-034 (this requirement's own scope also bundles pointer + capacity, unlike REQ-RMAP-033's pointer-only scope).

**Lesson for this codebase's own ongoing audit discipline**: a requirement's "what remains open" clause can silently go stale the moment a *different*, later requirement's own PR closes the gap it names — grep for a pointer/field's own name across every requirement mentioning it, not just the requirement bearing its own ID, before scoping new work as if a text's claim were still current. This is the third time this pattern has surfaced this session (see also REQ-RMAP-051/055, closed the same way inside PR #309).

`cfusa check`: 0 errors. `cfusa trace --req-coverage 100`/`--sec-tested 100`: both 100%. No rebuild required (no source/test changes).

### v0.249.0 -- 2026-08-11 (issue #308: EP0 write dispatcher now enforces lifecycle/writer/lock authorization for all 4 pointed-to tables)

**`rcp_regmap_ep0_decode_write_request()` (issues #301/#306) applied writes to HW_config, EP_ID_config, response-queue-config, and request-stream-cfg without consulting lifecycle state, writer identity, or the `svr_configuration_lock` W+ lock at all — any writer in any lifecycle state could rewrite any of them. Found while reviewing the RMAP requirement set's own remaining `partial` entries (REQ-RMAP-040/041/047/048/049/052/054/061 all cited the same gap independently).**

The dispatcher's own signature now takes `rcp_lifecycle_state_t state` and `rcp_lifecycle_writer_ctx_t writer` (matching every other write path in this codebase), and each of its 4 routing blocks authorizes before applying, per that table's own TC18-derived access type: HW_config uses `RCP_LIFECYCLE_FIELD_HW_GENERIC`; EP_ID_config uses `rcp_lifecycle_field_writable_w_plus()` against `svr_configuration_lock`; request-stream-cfg uses `RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR`; response-queue-config — whose own 10-octet row mixes W+ octets (STREAM_UID, flush_on_count, Flush_time) with W* octets (Max_AVTPDUsize, queue_size) within one table — gets a new `respqueue_cfg_row_write_authorize()` helper that classifies every touched octet's row-relative offset and requires whichever type(s) the write's own byte span actually touches to independently authorize.

**A genuine conformance bug caught and fixed before merge**: HW_config was initially wired to `FUNCTIONAL_W_STAR`, matching its own generic "R/W*" wire-table column legend. Re-checking TC18 §12.7.6's own surrounding prose ("This configuration table can only be changed in the life-cycle state HW_unconfigured. In other states of the life cycle this is read-only") showed this is a table-specific override narrower than `FUNCTIONAL_W_STAR`'s own general rule, and exactly what `RCP_LIFECYCLE_FIELD_HW_GENERIC` already models (writable only in `HW_UNCONFIGURED`, only via a discovery-stream writer). Fixed before merge, with the general lesson recorded: never trust a table's own generic wire-column legend without checking that table's own surrounding prose for a narrower override.

`svr_configuration_lock` (Table 18, REQ-RMAP-029, already-existing storage) resolved as the single shared "locked" parameter every W+ check in this dispatcher consults — no new storage was needed. Per `rcp_lifecycle_field_write_error_w_plus()`'s own documented precedence, an active lock always yields `RCP_ERROR_LOCKED_MEM_ACCESS` regardless of writer identity or state, never `RCP_ERROR_UNAUTHORIZED_ACCESS` (reserved for writer-specific denial when the underlying state would otherwise permit).

REQ-RMAP-040/041/051/052/054/055/061 flip to `implemented` (authorization was their own last remaining gap — REQ-RMAP-051/055 tracked the FUNCTIONAL_W_STAR/W+ primitives themselves being unwired from any register-map write path at all, closed by this same dispatcher change). REQ-RMAP-047/048/049 stay `partial` — authorization is closed for all three, but each has its own separate, unrelated remaining gap (no MACsec layer; no ack-routing runtime logic; no response-routing runtime logic). New REQ-RMAP-072 tracks the authorization mechanism itself.

**A real memory leak caught by CI's own Linux LeakSanitizer, invisible locally** (macOS's ASan build does not support LeakSanitizer at all — confirmed via `detect_leaks is not supported on this platform`): the new authorization test's own HW_config-permitted-write sub-case encoded a frame via `rcp_acf_encode_abb()` and never freed it before reassigning `frame` in the next sub-case. Fixed by adding the missing `rcp_bytes_free(&frame)` call; verified by an explicit encode/free call-count balance check across the whole test function (8/8) since local ASan couldn't re-confirm the fix directly.

New test `test_ep0_dispatcher_denies_unauthorized_writes_before_applying_or_bounds_checking` (6 sub-cases: `RCP_CONFIGURED` denies each of the 3 single-classification tables; `HW_UNCONFIGURED` with the lock set denies EP_ID_config while still permitting HW_config via a discovery-stream writer; response-queue-config denies its own W* and W+ sub-ranges independently, and denies a write spanning both sub-ranges via the touched, locked W+ octet). All 13 existing write-dispatcher call sites updated to pass `state`/`writer`.

Mutation-tested five ways (one per authorization check — HW_config, EP_ID_config, response-queue-config's row-offset boundary, request-stream-cfg, plus a re-test of HW_config after the HW_GENERIC fix); all five caught cleanly.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.248.0 -- 2026-08-11 (issue #306: request-stream-cfg, a fourth pointed-to table with the same finding, closed)

**REQ-RMAP-047/048/049 (request-stream-cfg) gain a full bidirectional wire codec, the same as issue #301's own three tables. A fourth pointed-to table, found while reviewing the RMAP requirement set's own remaining 25 `partial` entries, closed with the identical rigor.**

Direct primary-source verification of TC18 §12.7.7 Table 22 on both spec revisions (RC1 PDF pages 57-58; RC5 PDF page 66, renumbered "Table 24" due to RC5's own added SPI content — same table, addresses/widths identical across both revisions) confirms a 24-octet-per-request-stream wire stride. New `rcp_regmap_request_stream_cfg_render()`/`_apply_reconfig()` (regmap.h/regmap.c) give this table its own wire codec; `rcp_regmap_ep0_decode_write_request()`/`_encode_read_response()` gain a fifth/fourth routing block respectively, targeting `svr_request_stream_cfg_ptr`'s own extent.

**Three fields deliberately excluded from the wire codec, each with its own documented reasoning**: `rx_wd_action` (confirmed via direct page-image read of Table 22 on both revisions — no corresponding register exists anywhere in TC18); `configured` (a codebase-internal bookkeeping flag, not a TC18 concept); `rx_wd_timeout_ms` (its own existing `rcp_regmap_wd_timeout_ms_to_ticks()`/`_ticks_to_ms()` conversion, REQ-RMAP-050, needs a caller-supplied `ms_per_tick` this table's render/apply_reconfig signature has no natural place for, and unlike the width mismatches below, a conversion failure here is safety-relevant — left as its own deliberate follow-up).

**Two more content/wire width mismatches, both resolved via the established saturating precedent** (`flush_time_us`, issue #301 batch 3): `rx_stream_max_request_size` (`size_t` vs. 16-bit register) and `rx_safestate_sequencer` (`uint16_t` vs. 8-bit register) both saturate rather than wrap — wraparound would silently alias onto another valid, meaningfully-different value (0 meaning "no fragmentation supported"; some other actually-existing sequencer index for a safety-relevant field).

**The 8 independently-configurable bits at relative address 0x000D** are serialized using this codebase's own existing RC1-baseline 8-independent-bit content model, not RC5's later 4-combined-bit restructuring — already investigated and deliberately not restructured (task #97): this codebase's own richer model is a strict, lossless superset of RC5's collapsed encoding.

**A real, previously-unflagged content-modeling gap found and fixed along the way (REQ-RMAP-071)**: `rcp_regmap_request_stream_cfg_t` was missing `rx_ovrflw_safestate_enable` entirely — `regmap.h`'s own terminology-drift section (task #97) had already NAMED it as one of this codebase's own eight bits, and `rcp_e2e_overflow_should_enter_safe_state()` (e2e.h, REQ-E2E-030) already existed ready to consume it, but the struct itself never got the field. Added, zero-initialized to false, now part of the new wire codec.

New tests: `test_request_stream_cfg_apply_reconfig_patches_addressed_octets_only`, `test_request_stream_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched`, `test_request_stream_cfg_render_saturates_oversized_max_request_size_without_wrapping`, `test_request_stream_cfg_render_saturates_oversized_safestate_sequencer_without_wrapping`, `test_request_stream_cfg_render_packs_all_eight_bits_at_0x000d`, `test_request_stream_cfg_render_leaves_rx_wd_timeout_and_reserved_octets_zero`, and both dispatcher tests extended with a 5th/4th table's own apply+boundary cases.

Mutation-tested six ways (both saturation clamps, the packed-bit encode, the apply_reconfig bounds check, and both dispatcher routing conditions); all six caught cleanly. **A real ASan stack-buffer-overflow was also caught and fixed during this batch** — a test's own comparison buffer was sized for one row's worth of `rcp_regmap_request_stream_cfg_render()` output but called with `count=2`; fixed by sizing the buffer to match, not by weakening the assertion.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.247.0 -- 2026-08-11 (issue #301 batch 4: EP0 dispatcher READ side, batch 1-4 all closed)

**REQ-RMAP-040/041 (HW_config), REQ-RMAP-052/054 (EP_ID_config), and REQ-RMAP-061 (response-queue-config) all gain their READ side, closing the last shared gap issue #301's own finding left open. All four batches of issue #301 are now complete.**

New `rcp_regmap_ep0_decode_read_request()` decodes an incoming ACF_ABB READ request addressed to EP0 (same leading-2-octet-address payload shape as the write dispatcher's own request; the requested `read_size` comes from the ACF header's own `read_size_or_segment_num` field, matching every other read-request-carrying-a-size convention in this codebase). New `rcp_regmap_ep0_encode_read_response()` routes the decoded address across the identical four extents the write dispatcher already routes (Table 18 itself, HW_config, EP_ID_config, response-queue-config), reusing each table's own already-proven `render()` function -- no second copy of any wire codec. On a known extent, the response carries `min(read_size, that extent's own remaining length from addr)` real octets followed by zero-fill up to `read_size` -- the identical convention `rcp_regmap_general_encode_read_response()` already established for Table 18 alone, now generalized. On an unknown address, `*out_error` is `RCP_ERROR_EP_NOT_FOUND` and the returned `rcp_bytes_t` is zeroed, the same split the write dispatcher already uses for its own denials.

`read_size` is deliberately `uint8_t`, matching `rcp_regmap_general_encode_read_response()`'s own established precedent rather than the ACF header's wider 12-bit field -- every one of this dispatcher's own four routable extents comfortably fits real configurations well under 256 octets, and widening asymmetrically for just these two new functions would be inconsistent with every sibling read-response function in this codebase.

**Deliberately does NOT compose data across more than one extent** even when `addr + read_size` would span into a second one -- TC18 defines no rule for combining two distinct pointed-to tables into one response, and inventing one here would not be primary-source-derived. A caller wanting a second table's own data issues a second, separately-addressed read.

New tests: `test_ep0_read_dispatcher_routes_all_four_extents_and_unknown_addresses` (10 sub-cases: Table 18 exact-length read, Table 18 oversized read proving zero-fill-not-spillover, HW_config read + its own routing boundary, EP_ID_config read + its own routing boundary, response-queue-config read + its own routing boundary, an address matching none of the four extents, and `decode_read_request()`'s own ACF-level short-frame/wrong-op failures) -- every routing-boundary case written in from the start, per batch 2/3's own established mutation-testing lesson.

Mutation-tested two ways: disabling the shared response-slice helper's own zero-fill/bounds computation (caught by both a clean assertion failure on native AND a genuine ASan stack-buffer-overflow abort -- the mutation was a real out-of-bounds read, not just a logic bug), and loosening the HW_config routing condition's own upper bound (caught cleanly by its own dedicated boundary case). Reverted, full suite re-verified byte-identical both times.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

**Issue #301 is now fully closed: all four batches complete.** All three pointed-to tables (HW_config, EP_ID_config, response-queue-config) can be both written and read over the wire, using one shared, address-routed dispatcher generalizing Table 18's own already-established wire codec. Remaining gaps in each affected requirement are now narrowed to lifecycle-state write authorization alone (deferred to whatever caller eventually owns lifecycle-state context, matching REQ-RMAP-025's own established precedent) -- no addressing or wire-format ambiguity remains anywhere in this lineage.

### v0.246.0 -- 2026-08-11 (issue #301 batch 3: response-queue-config write dispatch)

**REQ-RMAP-061 (response-queue-config) closed the same way REQ-RMAP-040/041 (HW_config) and REQ-RMAP-052/054 (EP_ID_config) closed in v0.244.0/v0.245.0 -- same finding, same batch-ordered plan, last of the three tables in issue #301's own write-dispatch scope.**

Unlike HW_config/EP_ID_config, no render function existed for this table at all before this batch. New `rcp_regmap_response_queue_cfg_render()` (regmap.h/regmap.c) is the first one, serializing TC18's own exact 10-octet-per-queue wire stride confirmed via direct TC18.txt read (§12.7.9 Table 24, L3025-3048): `STREAM_UID`@0x0000, `Max_AVTPDUsize`@0x0002, `queue_size`@0x0004, `flush_on_count`@0x0006, `Flush_time`@0x0008. New `rcp_regmap_response_queue_cfg_apply_reconfig()` is the parse-side inverse (same patch-then-reparse idiom as the other two tables). `rcp_regmap_ep0_decode_write_request()` gains `response_queue_cfg`/`response_queue_cfg_count` parameters and a third routing block targeting `svr_response_stream_cfg_ptr`'s own extent. A remote client can now write response-queue-config over the wire, completing write access to all three of issue #301's target tables.

**Real content/wire width mismatch found and fixed in the same batch**: `rcp_regmap_response_queue_cfg_t.flush_time_us` is `uint32_t` (chosen to match `rcp_respqueue_should_flush_by_time()`'s own even-wider `uint64_t` parameter, not the wire), but TC18's own `Flush_time` register is only 16 bits. `rcp_regmap_response_queue_cfg_render()` now saturates (never wraps) a value exceeding `0xFFFF` to `0xFFFF` when serializing this field -- wraparound to a smaller value, worst case 0, would silently invert the field's own meaning to TC18's "flush only by count" encoding. No corresponding clamp exists on the parse side: a value read back off the 16-bit wire register can never itself exceed `0xFFFF`.

Still open, matching REQ-RMAP-040/041/052/054's own precedent: the READ side of the dispatcher doesn't exist yet for any pointed-to table, and this dispatcher doesn't itself enforce this table's own mixed R/W*/R/W+ access types (`STREAM_UID`/`flush_on_count`/`Flush_time` are R/W+, `Max_AVTPDUsize`/`queue_size` are R/W*) via `rcp_lifecycle_field_writable()`/`_writable_w_plus()` (deferred to whatever caller eventually owns lifecycle-state context).

New tests: `test_response_queue_cfg_apply_reconfig_patches_addressed_octets_only`, `test_response_queue_cfg_apply_reconfig_rejects_out_of_range_leaving_table_untouched`, `test_response_queue_cfg_render_saturates_oversized_flush_time_us_without_wrapping`, and the dispatcher test extended to 9 sub-cases (renamed `test_ep0_dispatcher_routes_all_three_pointed_to_tables_and_unknown_addresses`) including a dedicated routing-boundary case for response-queue-config, designed in from the start based on batch 2's own mutation-testing lesson (the inner `apply_reconfig()`'s own bounds check can mask a loosened outer routing condition unless a boundary case specifically isolates it).

Mutation-tested three ways: the render saturation clamp, `rcp_regmap_response_queue_cfg_apply_reconfig()`'s own out-of-range bounds check, and the dispatcher's own response-queue-config routing condition -- all three caught cleanly and deterministically on the first attempt (the routing-boundary case, learned from batch 2, was written in from the start rather than discovered after an undetected mutation). Reverted, full suite re-verified byte-identical all three times.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

**Issue #301's own write-dispatch scope (batches 1-3) is now complete: all three pointed-to tables (HW_config, EP_ID_config, response-queue-config) can be written over the wire.** Remaining: batch 4, the READ side of the dispatcher, for all three tables at once.

### v0.245.0 -- 2026-08-11 (issue #301 batch 2: EP_ID_config write dispatch)

**REQ-RMAP-052/054 (EP_ID_config) closed the same way REQ-RMAP-040/041 (HW_config) closed in v0.244.0 -- same finding, same batch-ordered plan, next table in line.**

New `rcp_regmap_ep_id_map_apply_reconfig()` (the parse-side inverse of the already-existing `rcp_regmap_ep_id_map_render()`, identical patch-then-reparse idiom to `rcp_regmap_hw_pin_map_apply_reconfig()`). `rcp_regmap_ep0_decode_write_request()` gains two new parameters (`ep_id_map`, `ep_id_map_count`) and a new routing block targeting `svr_ep_bytebus_id_map_ptr`'s own extent, inserted between the HW_config routing block and the final unknown-address fallback. A remote client can now write EP_ID_config over the wire, the same way it could already write HW_config.

Required relocating the entire "EP0 address-routed dispatcher" section in `regmap.h` to the very end of the file, immediately before the closing `#ifdef __cplusplus` boilerplate: the dispatcher's own signature now references both `rcp_regmap_hw_pin_map_entry_t` and `rcp_regmap_ep_id_map_entry_t`, and C requires each to already be declared at the point of use -- this dispatcher cannot live any earlier in the header than the last of the tables it routes to.

Still open, matching REQ-RMAP-040/041's own precedent: the READ side (an address-routed `rcp_regmap_ep0_encode_read_response()`) doesn't exist yet for any pointed-to table, and this dispatcher doesn't itself enforce EP_ID_config's own access type via `rcp_lifecycle_field_writable()` (deferred to whatever caller eventually owns lifecycle-state context).

New tests: `test_ep_id_map_apply_reconfig_patches_addressed_octets_only`, `test_ep_id_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched`, and the existing dispatcher test renamed/extended to `test_ep0_dispatcher_routes_table18_hw_config_ep_id_config_and_unknown_addresses` (7 sub-cases, including a boundary case one octet past EP_ID_config's own extent -- added specifically because a mutation-testing pass found the dispatcher's own upper-bound routing condition was NOT independently exercised by any other case: `apply_reconfig()`'s own internal bounds check happened to mask a loosened routing condition for addresses that were merely still out-of-range within the (wrongly) widened window).

Mutation-tested two ways: loosening `rcp_regmap_ep_id_map_apply_reconfig()`'s own out-of-range bounds check, and loosening the dispatcher's own EP_ID_config address-range routing condition -- both produced clean, deterministic assertion failures once the boundary test above was added. Reverted, full suite re-verified byte-identical both times.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.244.0 -- 2026-08-11 (RMAP addressing architecture resolved; issue #301)

**User-directed re-investigation: the shared "how does a client's request address relate to a Table 18 pointer's value" question, left genuinely unresolved across the whole RMAP phase (issue #200), is now answered.**

Direct verification of the current RC5 baseline PDF (`OA_TC18_specification_v_0.5.1_RC_5_3624.pdf`, page 61) shows Table 18's own address column is explicitly headed **"Absolute address"** — this codebase's own prior comments throughout `regmap.h` mis-cited it as "relative address" (now corrected, 25+ occurrences, scoped precisely to Table 18's own field comments; HW_config's own row-address citations, confirmed genuinely "Relative Address" against the current PDF's own Table 21, deliberately left untouched — a different, still-correct concept for a different table).

This confirms every `_ptr` field in Table 18 shares ONE continuous address space scoped to EP0 (`byte_bus_id=0`): a pointer field's own value is itself an absolute address in that same space. A client reaches a pointed-to table (HW_config, EP_ID_config, response-queue config) via the same generic `evt[2:0]=111b` configuration mechanism every endpoint type already has a client-side encoder for (TC18 §12.7.1 Figure 19), targeted at `byte_bus_id=0` with `start_address` = the pointer's own current value.

Broader finding, tracked but out of scope for this PR: no endpoint type in this codebase has a **server-side** decode/dispatch for `evt=111b` requests at all, in either direction — every existing `apply_reconfig()` takes already-decoded raw payload bytes, and no read-side counterpart exists anywhere. Filed as [c-RCP#301](https://github.com/SoundMatt/c-RCP/issues/301) with the full architecture plan and batch order.

**REQ-RMAP-040/041 (HW_config) closed as far as this batch goes**: new `rcp_regmap_hw_pin_map_apply_reconfig()` (the parse-side inverse of the already-existing `rcp_regmap_hw_pin_map_render()`, same patch-then-reparse idiom every other endpoint type's own `apply_reconfig()` uses) and `rcp_regmap_ep0_decode_write_request()` (the generalized, address-routed dispatcher generalizing `rcp_regmap_general_decode_write_request()` to route between Table 18's own extent — always denied, reusing REQ-RMAP-025's own logic — and HW_config's own extent — applied). A remote client can now write HW_config over the wire. Still open: the READ side (an address-routed `rcp_regmap_ep0_encode_read_response()`) doesn't exist yet, and this dispatcher doesn't itself enforce HW_UNCONFIGURED-only writability (deferred to whatever caller eventually owns lifecycle-state context, matching REQ-RMAP-025's own established precedent).

New tests: `test_hw_pin_map_apply_reconfig_patches_addressed_octets_only`, `test_hw_pin_map_apply_reconfig_rejects_out_of_range_leaving_table_untouched`, `test_ep0_dispatcher_routes_table18_hw_config_and_unknown_addresses` (full 5-case coverage: Table 18 rejection, HW_config apply, HW_config out-of-range, unknown address, ACF-level frame failure).

Mutation-tested two ways: loosening the out-of-range bounds check, and loosening the HW_config address-range routing condition — both produced clean, deterministic assertion failures (the second one caught by two different tests simultaneously). Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.243.0 -- 2026-08-11 (additive, zero blast radius to existing callers)

**Phase 5e batch 3 (issue #201): REQ-TIMED-013, the missing ACF_ABB-over-TSCF timed-request encoder.**

`rcp_timed_encode_request_tscf()` (request_timed.h/request_timed.c) provides TC18 §11.2/§11.2.1's second encoding path for a timed request: a plain ACF_ABB message (no request_type opcode, no repurposing trick, unlike `rcp_timed_encode_request()`'s own NTSCF-only path) wrapped in a TSCF header whose `avtp_timestamp` carries the presentation time. A thin, named composition of two already-existing, independently tested primitives (`rcp_acf_encode_abb()`, `rcp_avtp_encode_tscf()`) — a caller could already compose them directly, but `request_timed.h` is where a caller reasoning about "timed requests" as a concept should find both of TC18's own encoding paths.

REQ-TIMED-013 stays `partial`: the wire shape is now correctly produced, but nothing on the decode/admission side interprets it as a timed request yet — `rcp_tsn_classify_frame()` correctly classifies a TSCF-wrapped ACF_ABB frame as `RCP_SCHED_KIND_STANDARD` at the request-kind level ("timed" is an orthogonal AVTP-header-level property, not a distinct kind), and REQ-TIMED-012's own separate, larger gap (TSCF's `avtp_timestamp` never reaches the admission/due-selection path) means a server built on this library doesn't yet honour the presentation time this encoder now correctly transmits. REQ-TIMED-012 remains its own, substantially larger, deliberately deferred item.

New tests (`tests/test_request_timed.c`): `test_tscf_request_round_trip` (full decode-both-layers verification: TSCF header's `avtp_timestamp`/`tv`/`sequence_num`/`stream_id`, ACF_ABB's own `byte_bus_id`/`op`/`transaction_num`/`mtv`/payload), `test_tscf_request_zero_payload`, `test_tscf_request_rejects_oversized_payload`.

Mutation-tested: forcing `tv` (timestamp-valid) to 0 regardless of the function's own intent produced a clean, deterministic assertion failure. Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.242.0 -- 2026-08-11 (behavioral fix, tests-only blast radius)

**Phase 5e batch 2 (issue #201): REQ-LINEP-023, the LIN transmission-done trigger now honours the required AND condition. REQ-I2C-019 and REQ-LINEP-024 confirmed already `implemented` -- their groups' remaining item counts in issue #201 were stale.**

`rcp_ep_lin_trigger_fires()` (ep_lin.h/ep_lin.c) gains a second parameter, `trailing_time_expired`, ANDed with `tx_done_event` for `RCP_EP_LIN_TRIGGER_TX_DONE` -- TC18 §13.7.10.1's own text, verified directly against TC18.txt: "The LIN EP issues a trigger when a transmission has been finalized, and the configured trailing time has expired." Table 52 (§13.7.10.2) defines no dedicated wire register for "the configured trailing time" itself -- like this endpoint type's own trigger concept as a whole (already documented as having no TC18 basis, entirely original design), the caller supplies this as an already-classified boolean, matching the same convention every endpoint module's own trigger-evaluation function already uses. REQ-LINEP-023 moves `partial` -> `implemented`: the previously-missing AND condition is the requirement's whole remaining scope, and it's now correctly modeled.

Blast radius: 7 existing test call sites (2 files) needed updating for the new parameter (a compile-time argument-count mismatch, not silently discardable like a bool return); no production caller existed.

New tests: `test_trigger_fires` (test_ep_lin.c) extended to the full 2×2 matrix for TX_DONE; `test_lin_trigger_now_honours_trailing_time_and_block_has_registers` (test_tc18_gaps_ep2.c, renamed from `..._ignores_trailing_time_...`) rewritten to assert the fix rather than pin the deviation.

Mutation-tested: removing the `&&` (fires on `tx_done_event` alone) produced clean, deterministic assertion failures in both affected test files. Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.241.0 -- 2026-08-11 (zero production blast radius -- no prior caller existed)

**Phase 5e batch 1 (issue #201): REQ-DISC-029, discovery-stream-occupied refusal now has a real signal.**

`rcp_discovery_claim_note_request()` (discovery.h/discovery.c) changes `void` -> `bool`: `true` when the discovery claim was open and granted, `false` when refused because it was already held by an unlapsed claimant. TC18 §12.3 Figure 16's own two "Discovery request received" transitions carve out no exception for requester identity, so this refusal applies uniformly whether a different client or the current claimant itself re-requests -- confirmed via direct TC18.txt read of both diagram transitions.

Zero production blast radius: this function had no caller anywhere in the codebase besides its own definition before this change (still true after -- no wire-dispatch path calls it yet, same deferred-dispatch pattern as the rest of this project's TC18-gap work). Every existing test call site (11 across 3 test files) compiles unchanged, since C permits silently discarding a return value.

**Genuinely left open, not force-resolved**: `DISCOVERY_STREAM_OCCUPIED` is a Figure-16-diagram-only label -- TC18 §12.9.6 Table 27's own 17 numbered wire error codes (`rcp_wire_error_t`) do not include it. Unlike `LOCKED_CONFIG_ACCESS` (which cleanly maps onto `RCP_ERROR_LOCKED_MEM_ACCESS`, the only numbered code with a semantically matching name), no numbered code here has an obviously corresponding meaning, so none is invented. Which wire error code (if any) a future caller should send for the `false` case remains a genuine, unresolved ambiguity, same class as `REQ-ACF-012`'s `RCP_ACF_MTV_UNCERTAIN`.

New test: `test_discovery_claim_refusal_now_returns_a_real_signal` (replaces the old gap-documentation test, which is now stale since the refusal is no longer "unreportable" -- it also newly exercises the same-claimant-re-request case, which the old test never covered).

Mutation-tested: granting the claim unconditionally regardless of `rcp_discovery_claim_is_open()`'s answer produced a clean, deterministic assertion failure. Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.240.0 -- 2026-08-11 (additive, zero blast radius to existing callers)

**Phase 5d Group 5 batch 3, closing out issue #200's originally-scoped RMAP work: REQ-RMAP-055, the W+ lockable access type.**

`rcp_lifecycle_field_writable_w_plus()`/`rcp_lifecycle_field_write_error_w_plus()` (lifecycle.h/lifecycle.c) implement TC18's W+ access type (§12.7.8 Table 23's EP_ID_config rows; §12.7.9 Table 24's STREAM_UID/flush_on_count/Flush_time queue registers): the same lifecycle-state/writer rule as `RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR`, plus an independent lock the configuring instance may set at any time to protect the table from further modification "independently of the lifecycle state that governs W and W*" (TC18's own words).

Deliberately implemented as a **separate function pair**, not a new `rcp_lifecycle_field_kind_t` enum value threaded through `rcp_lifecycle_field_writable()`'s own signature. That function has ~90 existing call sites (every endpoint type's own writability gate, 2 internal uses in lifecycle.c itself, regmap.c, and every test exercising any of them) — none of which have any use for a lock concept. A standalone pair delivers the identical TC18-conformant primitive with zero risk to any existing caller, reusing (not re-deriving) the existing `FUNCTIONAL_W_STAR` state/writer rule internally rather than duplicating it.

REQ-RMAP-055 moves `not-implemented` → `partial`: the primitive is real and tested, but no register-map write path in the codebase calls it yet — EP_ID_config and the Table 24 queue registers are both still content-modeling-only (REQ-RMAP-052/054/061/065), the same deferred ACF_ABB-wire-wrapper gap as the rest of this phase.

New test: `test_w_plus_field_now_has_a_real_lockable_primitive` (replaces the old gap-documentation test, which incorrectly characterized `rcp_lifecycle_field_kind_t`'s pre-existing `READ_ONLY = 3` value as "unrecognized" — a separate, pre-existing test inaccuracy also corrected here).

Mutation-tested two ways: removing the independent-lock check, and swapping the reused state rule from `FUNCTIONAL_W_STAR` to plain `FUNCTIONAL_W` — both produced clean, deterministic assertion failures. Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

**RMAP status after this batch (70 total requirements, issue #200)**: 45 `implemented`, 25 `partial` (each honestly gated on a documented, separate architecture/dispatch gap), **0 remaining `not-implemented`**. Every item originally scoped into issue #200's 6 groups has now been addressed to the maximum honest degree possible without resolving genuinely unresolved primary-source ambiguities (Table 33/36's own address collision) or building mechanisms this codebase doesn't have yet (bit-level register writes, ACF_ABB pointer-table dispatch for HW_config/EP_ID_config/response-queue).

### v0.239.0 -- 2026-08-11 (doc + content, no behavior change to any existing consumer)

**Phase 5d Group 5 batch 2 (issue #200): Table 33/36 (RC Server functional configuration) investigated and content-modeled where honest to do so; a prior "PDF garbling" assumption corrected.**

Direct PDF page-image reads (not `pdftotext` extraction) of TC18 §13.7.1.2's own table on both the RC1 baseline (p.81, "Table 33") and the current RC5 baseline (p.91, renumbered "Table 36" by RC5's own added content) confirm the earlier-session "two-column PDF garbling" suspicion for this table was **wrong**: the table has a genuine, primary-source address collision present in both revisions -- `0x0002`/`0x0003`/`0x0004` are each assigned twice, once to the generic EP_FUNC common-entries fields (`svr_ep_enable&clr`/`svr_ep_options`) and once to RC-Server-specific fields (`svr_root_client_index`/`svr_lifecycle_state`/`svr_ep_status`) -- the same class of defect already documented for CAN's Table 53/56. A second, independent contradiction was also found: §13.7.1.1's own prose, immediately preceding this table, states "the RC Server as endpoint is not included in the EP_FUNC_config register maps" -- yet the table lists the generic EP_FUNC common-header fields for the RC Server anyway.

**New**: `rcp_regmap_svr_ep_cfg_t` (regmap.h/regmap.c) models only the two fields free of both defects: `svr_discovery_timeout` (REQ-RMAP-066, defaults to TC18's own stated 20000 µs = 20 ms) and `svr_ep_status` (REQ-RMAP-067). Deliberately NOT modeled: the four common-header fields (would require silently picking a side of the stated self-contradiction) and `svr_root_client_index`/`svr_lifecycle_state` (already correctly modeled at their own uncontested Table 18 addresses -- REQ-RMAP-038/023 -- not duplicated under this table's own disputed local addressing). regmap.h's own new file-header section documents the full investigation, including a working-but-unconfirmed hypothesis (an off-by-4, forgot-the-common-header-offset authoring error) for the collision, explicitly not coded as fact.

**REQ-RMAP-068 reassessed, not force-implemented.** Its own citation (§13.7.1.2's prose) turns out to describe two things, not one: the state-vs-writer distinction (`RCP_ERROR_LOCKED_MEM_ACCESS` vs `RCP_ERROR_UNAUTHORIZED_ACCESS`) is already correctly implemented by `rcp_lifecycle_field_write_error()` (REQ-WIREERR-004/REQ-LIFECYCLE-024) -- this requirement's own prior text claiming "no code path maps the two cases" was stale, now corrected. What remains genuinely open is a THIRD outcome the same paragraph's preceding sentence describes ("read only registers has no effect and request is confirmed normally", err=0, not an error at all) that neither implemented code produces. Working hypothesis: this describes individual read-only BITS within an otherwise-writable register (the same paragraph's OR/AND/XOR/SET register-write-operation discussion), a bit-level concern distinct from -- and NOT a correction to -- REQ-RMAP-025's own already-correct, twice-independently-verified whole-register-map access control (Figure 16, `RCP_LIFECYCLE_FIELD_READ_ONLY` → `LOCKED_MEM_ACCESS`), which is deliberately left unchanged. Not implementable yet regardless: no register-write dispatch mechanism in this codebase currently operates at the bit level.

New test: `test_svr_ep_cfg_now_models_discovery_timeout_and_status` (replaces the old gap-documentation test for REQ-RMAP-066/067, which now correctly fails once the gap partially closed).

Mutation-tested: reverting `rcp_regmap_svr_ep_cfg_init()`'s power-on default produced a clean, deterministic assertion failure (single full-revert mutation, matching batch 10's own established "pure field addition, no surrounding logic" calibration -- no paired logic mutation needed). Reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

**Deliberately deferred, unchanged**: REQ-RMAP-055 (W+ lockable access type) remains its own separate, out-of-scope item.

### v0.238.0 -- 2026-08-11

**Phase 5d Group 5 batch 1 (issue #200): Table 22's three remaining routing indices modeled, watchdog tick/millisecond conversion added.**

`rcp_regmap_request_stream_cfg_t` (regmap.h) gains `rx_secure_channel_index` (REQ-RMAP-047, TC18 §12.7.7 Table 22, 0x000C), `rx_ack_stream_index` (REQ-RMAP-048, 0x0010), and `rx_resp_stream_index` (REQ-RMAP-049, 0x0011). All three zero-initialize except `rx_resp_stream_index`, which `rcp_regmap_request_stream_cfg_init()` now sets to 1 — TC18's own deliberate bootstrap guarantee (a freshly reset server can answer a discovery request before any configuration has been written). Content modeling only: the same deferred ACF_ABB-wire-wrapper gap already tracked for HW_config/EP_ID_config/response-queue applies here too, so REQ-RMAP-047/048/049 all stay `partial`, not `implemented`.

**Requirement-text conflict caught and fixed before implementing**: REQ-RMAP-018's own existing text ("rcp_regmap_request_stream_cfg_init() shall set configured to false and every other field of its argument to 0") directly contradicted the planned `rx_resp_stream_index = 1` default. Resolved by correcting REQ-RMAP-018's own text to carve out the one deliberate exception, per this project's standing "verify against current requirement state before implementing" discipline.

REQ-RMAP-050 (watchdog timeout register width/unit): `rcp_regmap_wd_timeout_ms_to_ticks()`/`_ticks_to_ms()` (regmap.h/regmap.c) perform the ms↔clock-tic conversion and 16-bit bounds check TC18 §12.7.7 Table 22 requires at the register-write boundary for `rx_wd_timeout_intervall` (0x000A). TC18 names no fixed clock-tick rate for this register anywhere near its own definition (unlike, e.g., PWM's own endpoint-local "clock selected for this endpoint" phrasing), so both functions take the tick duration as a caller-supplied parameter, matching the established caller-supplies-already-classified-units convention (`rcp_acf_reg_write_len()`, `rcp_respqueue_max_avtpdu_size_within_mtu()`). ms-to-ticks rounds down, not up: a requested watchdog period that does not divide evenly into whole tics is truncated, so the register's enforced period is never longer than requested — a safety-integrity register should never silently grant more slack than asked for. Stays `partial`: no register-write code path calls these functions yet, the same deferred-dispatch gap as the rest of Table 22.

New tests: `test_request_stream_cfg_now_has_channel_and_stream_indices` (replaces the old "lacks" gap-documentation test), `test_watchdog_timeout_internal_unit_is_still_milliseconds` (replaces the old "deviate" gap-documentation test), `test_wd_timeout_ms_to_ticks_rounds_down_and_bounds_checks`, `test_wd_timeout_ticks_to_ms_round_trips`.

Mutation-tested: removing the 16-bit ceiling check produced a clean, deterministic assertion failure; changing ms-to-ticks rounding from down to up (`(timeout_ms + ms_per_tick - 1) / ms_per_tick`) also produced a clean, deterministic assertion failure. Both reverted, full suite re-verified byte-identical.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

Table 33 (REQ-RMAP-066/067/068) deliberately deferred to a separate batch: zero existing content model plus a still-unresolved primary-source address-layout ambiguity (a two-column PDF extraction issue flagged in an earlier session) that needs direct PDF-page verification, not `pdftotext -layout` extraction, before any implementation is attempted.

### v0.237.0 -- 2026-08-11

**Phase 5d Group 4 remainder (issue #200): REQ-RMAP-061's own MTU-consistency-check half closed.**

`rcp_respqueue_max_avtpdu_size_within_mtu()` (respqueue.h/respqueue.c) is the config-time check TC18 §12.7.9 requires ("the Max_AVTPDUsize shall always be configured such that the final network frame does not exceed the maximum transmit unit size of the network", TC18.txt L3010-3011) — run before ever calling `rcp_respqueue_init()`. TC18 defines no fixed MTU value of its own, so `mtu_budget_octets` is the caller's own already-adjusted ceiling, matching this module's established "caller supplies already-classified units" convention throughout. `max_avtpdu_size_octets == 0` (this module's own "unbounded" convention) is never within budget for a nonzero MTU budget — an unbounded ceiling cannot be MTU-safe by definition — except the degenerate case where both are 0.

REQ-RMAP-061 stays `partial` overall: TC18 §12.7.9's own Table 24 is a separate table pointed to by Table 18's own `svr_response_stream_cfg_ptr`, not reached via Table 18 itself or an endpoint's own EP_func block — the same genuine, unresolved ACF_ABB addressing question already documented for HW_config and EP_ID_config. **Also corrected**: this requirement's own prior text described the remaining gap as "exposing the value in the discovery general-register slice" — that framing predates this codebase's own later discovery that Table 24 was never part of the 14-octet discovery slice at all.

REQ-RMAP-065 (empty-queue heartbeat) reviewed and confirmed already honestly scoped — its own remaining gap is a real integrator responsibility (this is a protocol library, not a scheduler, matching REQ-SRV-017's own precedent), not a code gap. No change.

New test: `test_max_avtpdu_size_within_mtu_check` covers the ordinary/boundary/over cases plus both unbounded-input edge cases.

Mutation-tested: relaxing the unbounded-ceiling special case to always return `true` produced a clean, deterministic assertion failure. Reverted, full suite re-verified clean.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.236.0 -- 2026-08-11 (doc + structural, no behavior change to any existing consumer)

**Phase 5d Group 3 remainder (issue #200): EP_ID_config gets a correct 4-octet-per-row wire stride, and a stale cross-reference is corrected.**

`rcp_regmap_ep_id_map_render()` (regmap.h/regmap.c) serializes a real table at TC18 §12.7.8 Table 23's own exact 4-octet-per-row stride (request_stream_index/ep_id/byte_bus_id at row offset 4*N), proven via a byte-offset test — including `ep_id`'s own honest truncation to the wire's real 8-bit EP_Nr width (this module's own in-memory `ep_id` is 16-bit, matching every other endpoint-index field in this codebase), the same documented-truncation convention ADC's own render path already established.

Same architecture caveat as HW_config (v0.235.0): the ACF_ABB wire request/response wrapper is **not** implemented. EP_ID_config is a separate table pointed to by Table 18's own `svr_ep_bytebus_id_map_ptr` (REQ-RMAP-037), not reached the way Table 18 itself or an endpoint's own EP_func block are — the exact same genuine, unresolved addressing question. REQ-RMAP-052/054 both stay `partial`, not `implemented`.

**Also fixed**: REQ-RMAP-052's own `.fusa-reqs.json` text cited REQ-RMAP-056 as "its own separate still-open scope" — REQ-RMAP-056 was actually already closed in an earlier batch (confirmed both by `.fusa-reqs.json`'s own current status and by `regmap.h`'s own field comment, which already correctly said "closed as of this field's own follow-up batch"). The catalog text alone was stale; corrected.

Mutation-tested: swapping which field the render function writes at row offset 0 (`request_stream_index` → `ep_id`) produced a clean, deterministic assertion failure. Reverted, full suite re-verified clean.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.235.0 -- 2026-08-11

**Phase 5d Group 2 (issue #200): HW_config now has real server-side storage and a correct 3-octet-per-pin wire layout.**

Investigated §12.7.6's own text directly before writing code: HW_config is a *separate* table from Table 18, pointed to by Table 18's own `svr_hw_cfg_ptr` register, with Table 19's own address column headed "Relative Address" (not absolute) and R/W* access ("This configuration table can only be changed in the life-cycle state HW_unconfigured"). Unlike Table 18 -- reached via a plain read always addressed at 0 -- precisely how a client's request address relates to `svr_hw_cfg_ptr`'s own value is a genuine, unresolved architectural question (TC18's own §12.7.1 Figure 18 configuration-request mechanism is described in per-*endpoint* EP_func terms; HW_config isn't an endpoint's EP_func at all). Rather than guess, the ACF_ABB wire wrapper is deliberately **not** attempted this batch -- REQ-RMAP-040/041 both stay `partial`, not `implemented`, for exactly this reason.

What **is** real now: `rcp_mock_server_t` carries an actual bounded HW_config table (`rcp_mock_server_set_hw_pin_map()`/`_hw_pin_map()`, mock.h/mock.c, `RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES` = 64) instead of no storage at all, and `rcp_config_apply_to_mock()` no longer silently discards the parsed manifest's `hw_pin_map` -- it populates the new table for real (`rcp_config_hw_pin_t` and `rcp_regmap_hw_pin_map_entry_t` are field-for-field identical, so this is a straight copy, no conversion logic needed). `rcp_regmap_hw_pin_map_render()` (regmap.h/regmap.c) serializes a real table at TC18's own exact 3-octet-per-pin stride (IO_Pin N at relative address 3*N), proven directly via a byte-offset test across two rows rather than merely inferred from field order.

Two of the three pre-existing Group 2 deviation-pin tests rewritten positive (`test_hw_config_table_now_has_real_server_side_storage`, `test_hw_config_row_stride_now_modeled_gpio_access_class_still_diverges` -- the latter's own GPIO-vs-HW_config access-class comparison, a separate architecture question tracked against REQ-GPIO-013, is retained unchanged); one new test (`test_hw_pin_map_rejects_oversized_table_leaving_existing_data_intact`).

Mutation-tested two ways: removing the oversized-table bounds check corrupted adjacent struct data, caught as a clean, deterministic assertion failure (not always ASan-visible, since the overflow write lands within the same heap allocation, not past its edge -- the test's own explicit return-value check is what catches it). Narrowing the render function's own byte stride (`3u * i` → `2u * i` for one field) produced a clean, deterministic assertion failure. Both reverted, full suite re-verified clean.

65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.234.0 -- 2026-08-11

**Phase 5d Group 1 (issue #200): the RC Server general register map's full TC18 §12.7.5 Table 18 extent is now wire-reachable, not just its leading 14-octet discovery slice.** Closes REQ-RMAP-024, the umbrella wire-reachability gap that kept 15 sibling requirements' own field-level fixes stuck at `partial` despite their in-memory content already being correct.

TC18 §12.7 itself defines the mechanism this closes: "Access to the configuration and status information is per ABB or GBB messages... the RC Server exposes itself as endpoint0 (EP0)" -- the same plain ACF_ABB read request `rcp_discovery_encode_request()`/`_decode_request()` already build, just serving more of the register map than the discovery handshake's own deliberately narrow 5-field/14-octet identity slice (`RCP_DISCOVERY_GENERAL_SLICE_LEN`, left completely unchanged by this PR).

New in `regmap.h`/`regmap.c`: `rcp_regmap_general_render()` serializes every genuine Table 18 field at its own TC18-cited address into a `RCP_REGMAP_GENERAL_LEN` (0x0040-byte) wire image; `rcp_regmap_general_encode_read_response()`/`_decode_read_response()` wrap that in the ACF_ABB read-response shape (short `read_size` values still work, carrying only a prefix); `rcp_regmap_general_decode_write_request()` recognizes any write attempt into this space and reports `RCP_ERROR_LOCKED_MEM_ACCESS` every time, via lifecycle.h's already-proven `RCP_LIFECYCLE_FIELD_READ_ONLY` primitive (REQ-RMAP-025) -- reused, not duplicated.

Two struct fields, `svr_lifecycle_state` and `svr_root_client_index`, are deliberately excluded from the render: neither has a genuine Table 18 address (confirmed directly against the primary source -- Table 18's own address sequence has no room for either). Their real home is TC18 §13.7.1.2 Table 33 (the RC Server's own separate EP_FUNC_config block), still unimplemented (REQ-RMAP-023, REQ-RMAP-067) -- not force-mapped onto an invented address. A one-byte gap at relative address 0x002B (between two adjacent registers with no explicit "reserved" row in the primary source, unlike 0x0017/0x0022) is written as 0x00 and flagged as an inferred, unconfirmed alignment gap rather than assumed risk-free.

**9 requirements move to `implemented`**: REQ-RMAP-024/025/026/027/028/029/030/031/035 -- every one of these had no remaining gap beyond wire-reachability. **8 stay honestly `partial`**, text updated to remove the now-closed 024 dependency but keep their own real, separate gaps: REQ-RMAP-023 (Table 33, not this table), REQ-RMAP-032/033 (Group 2 HW_config storage doesn't exist yet), REQ-RMAP-034/036/037/038/039 (Group 3/4 sub-table storage doesn't exist yet) -- each now honestly says "wire-readable but currently always reads 0" instead of "unreachable."

New tests in `tests/test_tc18_gaps_regmap.c`: a byte-offset spot-check of `rcp_regmap_general_render()`'s raw output (distinct from the round-trip test -- proves the wire layout directly, not just that decode(encode(x))==x); a full round-trip test across every Table 18 field with the two deliberately-excluded fields poisoned beforehand to prove the exclusion is real; a short-`read_size` partial-population test; `strerror`/malformed-frame decode-path tests; the write-rejection test; and a test isolating `rcp_mock_server_regmap()`'s own still-mutable in-process pointer as a distinct, non-conformance-relevant design choice. 12 of the pre-existing Group 1 deviation-pin tests (originally proving *unreachability* via `read_general()`/`span_is_zero()`, both now dead code and removed) had their own doc comments and, where present, tail assertions updated to cross-reference the new central test instead of re-testing wire-reachability per field.

Mutation-tested two ways: loosening the short-response bounds clamp (`have = RCP_REGMAP_GENERAL_LEN` unconditionally) produced a silent ASan heap-buffer-overflow abort on the short-`read_size` test, proving the clamp is load-bearing, not just test-satisfying; inverting the write-request's own op check (`RCP_ACF_OP_WRITE` → `RCP_ACF_OP_READ`) produced a clean, deterministic `Expected 0 Was 4` assertion failure. Both reverted, full suite re-verified clean.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.233.0 -- 2026-08-11 (doc-only)

**CAN Table 53/56 register-block address defect, task #94 closed** (`c-RCP-AUDIT-06`, issue #256's own dedicated-investigation deferral list -- outside the 156-finding headline count, alongside the wakeup item PR #288 already closed -- rescoped to a pure spec-conformance finding per explicit user directive): re-verified TC18 §13.7.11.2's functional-configuration register table against the rendered PDF page image directly (not extraction alone) on both the 0.5.1_RC baseline and 0.5.1_RC5 revision -- identical content, table renumbered 53->56 only. Confirmed two genuine, unresolved defects: (1) "acceptance filter 3" and "acceptance filter 4" are both printed at relative address 0x002C, immediately followed by receive filter 1 at 0x0030 -- a real address collision, with a genuine, primary-source-undecidable ambiguity over whether only 3 acceptance filters really exist (receive filters already correctly addressed) or 4 do (in which case every receive-filter address needs a uniform +4-byte shift). (2) The three 32-bit bit-timing registers (Classical/FD/XL) are opaque, with no sub-field bit-layout published anywhere near the table in either revision -- not an extraction gap, the primary source itself never publishes it.

**No code change made**: a fresh audit of `ep_can.c` this session found zero non-spec-compliant code (the one historical defect in this module -- frame_format packed into evt[2:0] instead of the payload's leading quadlet -- was already fixed at v0.109.0, well before this session) and zero byte-level (de)serialization of Table 53/56's register block anywhere -- `rcp_ep_can_functional_cfg_t` is a pure in-memory, caller-populated API, matching the same "no wire (de)serialization = no live conformance risk" pattern task #97 established for the RC-server register block. Both defects above therefore carry zero live wire-conformance risk today.

A new file-header section in `ep_can.h` documents the collision and both possible readings without force-resolving which is correct, so a future implementer of a real register-block codec for this endpoint (the `render_registers()`/`apply_reconfig()` shape every other Group I/D endpoint now uses) must resolve it deliberately rather than silently copying whichever ordering seems convenient. `REQ-CANEP-029`'s citation corrected to state the collision honestly instead of implying a clean, non-colliding address range.

**This closes every dedicated-investigation item this audit's own tracking ever deferred**: issue #256's 156-finding catalog (Groups A-K, all already closed), the TC18 0.5.1_RC5 spec-rebaseline project (task #96/#97, already closed), the wakeup dedicated-investigation session (task #95, already closed), and now this, the last of the three items issue #256 itself named as deliberately outside its own 156-finding count. RMAP's own Phase 5d (issue #200, task #88) remains separately open and unaffected -- a distinct, older, still-ongoing effort, not part of this lineage.

65/65 both trees (native + ASan/UBSan, unaffected -- comment/citation text only). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.232.0 -- 2026-08-11 (mostly doc-only)

**RC-server §12.7.7 stream-config terminology drift, investigated, task #97 closed with a deliberately conservative outcome** (`c-RCP-AUDIT-06`): TC18 0.5.1_RC4 renames and restructures the whole 0x000D octet of the request-stream-configuration register. The 8 independently-configurable bits `rcp_regmap_request_stream_cfg_t` was originally modeled against (`rx_enforce_e2e`/`rx_enforce_seq`/`rx_seq_safestate_enable`/`rx_wd_enable`/`rx_wd_safestate_enable`/`rx_ovrflw_safestate_enable`/`rx_safety_measure`/`rx_wd_info_enable`) become, in RC5's own Table 24 (renumbered from Table 22), 4 combined bits (`rx_enforce_crc`/`rx_enforce_sequence`/`rx_enforce_watchdog`/`rx_enforce_request_filing`) plus 3 reserved bits and a new read-only `rx_stream_status` bit.

**No structural code change made**, for three reasons confirmed directly against the rendered PDF on both spec revisions: (1) this struct has zero wire (de)serialization anywhere in this codebase -- confirmed via grep across `src/`; nothing decodes a real 0x000D byte from the wire today, so there is no live conformance defect the old-vs-new bit layout could cause. (2) `rx_enforce_e2e`'s own RC5 rename (`rx_enforce_crc`) is a pure synonym, zero semantic change; the other 3 new bits each collapse what were two independently-configurable dimensions in the baseline into one combined bit, but `e2e.h`'s own `rcp_e2e_seq_evaluate()`/`_wd_evaluate()`/`_overflow_should_enter_safe_state()` deliberately keep those dimensions independently expressible ("deliberately NOT collapsed into one bool: they answer different questions" -- `e2e.h`'s own words) -- this codebase's richer model remains a strict, safe superset of what RC5's collapsed wire encoding can express, not a defect requiring narrowing. (3) `rx_safety_measure` (the high-impedance-vs-sequencer safe-state selector) and `rx_wd_info_enable` (the repetitive-notification-on-overflow feature) have no clear 1:1 replacement in RC5's own 4-bit scheme -- genuinely ambiguous, not resolved. The surrounding registers (`rx_safestate_sequencer`/`rx_safe_sequencer_state`, 0x000E/0x000F) are themselves flagged in the same RC5 revision as subject to a separate, still-draft "trigger request" harmonization proposal (already confirmed still-draft in an earlier rebaseline batch) -- reinforcing that a full structural rewrite of this octet would be premature.

Terminology cross-references added to `regmap.h`'s file header and each affected field's own doc comment, `e2e.h`'s file header, and `server.c`'s own `rx_ovrflw_safestate_enable` comment, so a future reader encountering the new RC5 register names can find the mapping without re-deriving it.

**One genuinely new capability found and honestly tracked, not implemented**: `rx_stream_status` (new `REQ-E2E-046`, `not-implemented`) is a passive, client-polled aggregate "is this stream currently blocked" status covering all four fault classes uniformly. This module has a real asymmetry that makes implementing it non-trivial: `rcp_e2e_stream_fault_t` already provides a persisted, later-queryable latch for the CRC case specifically, but sequence/watchdog/overflow each report only a per-call decision at the moment of the triggering event, with no equivalent persisted state to aggregate. Implementing `rx_stream_status` correctly needs a new, cross-cutting per-stream aggregate-latch primitive this codebase doesn't yet have -- not attempted, to avoid a rushed design of new ASIL-relevant state-tracking infrastructure. A new deviation-pin test (`test_e2e_has_no_aggregate_stream_blocked_status_across_all_four_fault_causes`) demonstrates the asymmetry directly.

65/65 both trees (native + ASan/UBSan, unaffected -- comments/JSON text plus one new test using only existing, already-tested functions). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.231.0 -- 2026-08-11

**WakeUp §12.7.1 register block implemented, dedicated investigation closed** (`c-RCP-AUDIT-06`, issue #256 Group I, task #95): closes the WakeUp deferred item -- TC18 §13.7.2.2 Table 36 has a genuine, literal address collision between `wup_status` and the wake-source array's own first entry (`wup_io_scr1`), both printed at the same relative address (0x0004). Confirmed via the rendered page image on **both** the 0.5.1_RC baseline and the 0.5.1_RC5 revision (identical on both -- not an extraction artifact, and unlike MDIO's own Table 56/59 collision, not independently corrected by the spec committee). Resolved via this session's own established cross-table pattern: `wup_status` keeps its own printed address, the wake-source array shifts to start immediately after it (0x0006), one slot per `RCP_EP_WAKEUP_MAX_SOURCES` (this module's own pre-existing upper bound, matching `wup_nr_io_pins_max` exactly).

Implemented `rcp_ep_wakeup_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`, mirroring every other Group I endpoint's own register-block pattern. New `ep_status` field (previously entirely unmodeled). New `pin_number` field on `rcp_ep_wakeup_source_cfg_t`.

Two further gaps handled conservatively (kept `partial`, not force-fit to `implemented`) rather than a from-scratch redesign: `wup_status` renders/parses only its own pre-existing single-aggregate-latch bit, not TC18's full 16-bit per-source bitmask -- the existing `rcp_ep_wakeup_wup_status_t` API and behavior are entirely unchanged, only wrapped in write-1-to-clear wire semantics. Each `wup_io_scrN` register renders/parses only 3 of Table 37's 6 IO_SRC values (inactive/high level/low level) -- `rcp_ep_wakeup_source_asserted()`'s own pre-existing level-only predicate is unchanged (edge-triggered detection would need previous-pin-level state its own tested API doesn't carry, and would ripple into every caller's own calling convention); a configuration write encoding an edge-triggered or reserved IO_SRC value leaves that slot's own `enabled`/`active_high` unchanged (an honest "cannot apply", not a silent misinterpretation) while `pin_number` still updates. `REQ-WAKEUP-021`/`022` both move from `partial`/`not-implemented` to `partial`; `REQ-CFG-011`/`012` updated to credit WakeUp (10 of 11 endpoint types now covered -- only CAN remains, deliberately deferred, task #94).

New tests: full register-block coverage (offsets, round-trip, write-1-to-clear, edge-triggered-value preservation, read-only-octet skip, short/out-of-range rejection, `reconfig_strerror`/`encode_reconfig_request`) in `test_ep_wakeup.c`; the pre-existing deviation-pin test in `test_tc18_gaps_ep.c` rewritten positive. Mutation-tested three ways: loosening the bounds check by 8 octets produces a silent ASan abort (real stack-buffer overrun, matching every prior Group I register-block batch); removing the write-1-to-clear bit-0 gate produces a clean, deterministic assertion failure; forcing the edge-triggered-value branch to overwrite `enabled`/`active_high` anyway produces a clean, deterministic assertion failure. All three reverted, re-verified clean.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.230.0 -- 2026-08-11 (doc-only)

**SPI channel-selection mechanism, dedicated investigation RESOLVED, no code change** (`c-RCP-AUDIT-06`, task #98): closes the question flagged and deferred across batches 2 and 3 of the spec rebaseline -- whether SPI channel selection should move from evt-bits to a new BBID-based `Channel_selection` field (Table 26). Read TC18 §13.5's own authoritative per-endpoint-type evt[2:0] table directly (the table whose own intro states "the detailed behavior of each endpoint based on the evt[2:0] is described in the following table"): the SPI row is completely unchanged in 0.5.1_RC5 (still "selects channel 0...5"), and its own attached tracked-change comment reads, verbatim, "051RC4: new concept: selection of SPI channel via Stream_id(index)/byte_bus_id (not via evt-bits) => this becomes obsolete IF new concept is accepted" -- explicitly conditional, and that condition is not met as of RC5. §13.7.3.1's own running prose (the earlier-flagged apparent inconsistency) has already been edited in place to describe the new concept as though adopted -- a real, minor internal inconsistency in the RC5 draft itself, not an extraction error. `ep_spi.h`'s existing evt-bits implementation is confirmed correct and fully conformant; a durable doc note recording this resolution (with the exact conditional quote) added to the file header so a future reader doesn't need to re-derive it from the PDF.

65/65 both trees (unaffected, comment-only). `cfusa check`: 0 errors.

### v0.229.0 -- 2026-08-11 (doc-only)

**Spec rebaseline to TC18 0.5.1_RC5, batch 3** (§12.7.8 EP_ID_config, doc-only, no code behavior change): confirmed the "Request_Stream_Index and BBID shall occur in ascending order" sentence `REQ-RMAP-020`/`021`/`022`/`056` (ASIL-A/QM) cite as their TC18 basis for `rcp_regmap_ep_id_map_is_ascending()` is entirely **deleted** as of spec revision 0.5.1_RC4 (rendered PDF, tracked-change tag `051RC4: sentence deleted as discussed`) -- current TC18 no longer states or implies any EP_ID_config ordering requirement at all. The function's own behavior is unaffected and remains a correct, harmless, purely-diagnostic helper (`regmap.h`'s own file header already correctly framed it as read-only tooling, never server-side enforcement) -- it simply no longer traces to a live TC18 MUST. All four requirements' `text`/`tc18` fields updated to record the deletion with primary-source evidence; `regmap.h`'s file header and the affected field's own doc comment updated to match.

**Also investigated, confirmed genuinely new (not a renaming) and deliberately NOT implemented**: TC18's own EP_ID_config table (renumbered Table 23 → Table 25) gained an entirely new `Ctrl1`/`Ctrl2` 5-bit field per BBID row (RC4), defined by a brand-new Table 26 "BBID control bits" -- `Channel_selection[3:0]` (RC4, explicitly footnoted as being for SPI's own channel selection) and `CRC_required` (RC5, ticket NXP_101). Confirmed via direct old-vs-new baseline text comparison that the 0.5.1_RC baseline's own `1_BBID`/`2_BBID` fields were plain, unsplit 16-bit values with no `Ctrl` concept at all -- this is genuinely new capability, not a rename. Directly overlaps the SPI channel-selection question already flagged and deferred in batch 2 (Table 26's own footnote confirms `Channel_selection` is that exact mechanism) -- folded into that same deferred investigation rather than duplicated, with materially stronger evidence now that it's a real, adopted, footnoted table rather than only a floating draft comment.

65/65 both trees (native + ASan/UBSan, unaffected since only comments/JSON text changed). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.228.0 -- 2026-08-11

**Spec rebaseline to TC18 0.5.1_RC5, batch 2**: SPI Table 42 (renumbered from Table 39). Two real deltas confirmed real via the PDF's own pages and already flagged in an earlier spot-check of this revision. (1) `spi_nr_cs` (0x0001) narrowed from a plain 8-bit count to a **4-bit `(count - 1)` field**, upper nibble reserved (spec revision RC4, "this standard limits the number of CS line per EP to 32") -- `rcp_ep_spi_render_registers()` now renders `(RCP_EP_SPI_MAX_CHANNELS - 1) & 0x0F` (0x05) instead of the plain count (0x06); `REQ-SPI-035` updated. (2) New `spi_deassert_cs_pauseN` bit (bit 4 of a channel's own +0x02 cfg octet, RC5 ticket NXP_100) with no counterpart in the baseline this module was built against -- added a new `deassert_cs_pause` field, `RCP_EP_SPI_CFG_BIT_DEASSERT_CS_PAUSE`, rendered/parsed alongside the pre-existing cfg bits (clk_polarity/clk_phase/cs_polarity/use_cs, all left untouched); new `REQ-SPI-040`. The SPI channel-selection mechanism itself (evt-bits vs. byte_bus_id) is confirmed, via the same page, to be a separate and still internally inconsistent question across the document as of RC5 -- flagged for its own dedicated investigation, deliberately not touched by this fix (register content only, not routing).

New tests: reserved-nibble-zero assertion on `spi_nr_cs`; a dedicated parse-path round-trip test for `deassert_cs_pause` (`test_apply_reconfig_writes_deassert_cs_pause_bit`) proving it round-trips independently of the other three cfg bits. Mutation-tested both changes: reverting the nr_cs encoding alone reproduces 3 clean failures across 2 test files (`Expected 5 Was 6` / `Expected 0x05 Was 0x06`); reverting the `deassert_cs_pause` parse line alone reproduces a clean `Expected TRUE Was FALSE`. Both reverted, re-verified clean.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.227.0 -- 2026-08-11

**Spec rebaseline to TC18 0.5.1_RC5, batch 1** (`c-RCP-AUDIT-06`): this codebase's TC18 conformance work has read against the 0.5.1_RC baseline (2026-07-14, 117pp) exclusively since the project began. A newer revision, 0.5.1_RC5 (2026-07-31, 125pp), has since superseded it through 4 intermediate point-releases (RC2/RC3/RC4/RC5), each with tracked-changes markers in the PDF body. Starting a systematic reconciliation using the PDF's own front-matter "Version and Restriction History" table as the map of every dated delta, cross-referencing each against the ~63 `Commented [XXX]` tracked-change markers in the document body, and triaging each as either a genuinely normative (adopted) delta or a still-draft proposal not yet accepted (e.g. RC3's "trigger request"/"compare request" harmonization, explicitly marked "New proposal" with "requests to be replaced are not yet deleted from spec" -- not implemented, matching the precedent already established for SPI's own proposed BBID-based channel-selection concept).

**Real bug found and fixed**: `acf.h`'s `rcp_acf_reg_write_len(acf_msg_length, pad)` computes the effective number of register-write data octets for an EP0 register-map write. The 0.5.1_RC baseline's own formula -- "Effective number of bytes to be written = (acf_msg_length - 3) x 4 - pad" -- omitted a term for the 2-octet register start address that leads the payload (TC18's own Figure 22 clearly places it there); this function matched that omission exactly. RC5 corrects the formula to "... - pad - 2" (tagged `051RC5: Formular corrected`, ticket NXP_101) and clarifies, in the same passage, that EP0 is *always* accessed in safe command mode (no longer conditional on the request) -- a second, related clarification whose full code impact (does a corresponding CRC-byte accounting also need to change?) is flagged, not yet resolved, since no production caller of this function exists yet to depend on either answer. Fixed the formula to match RC5 exactly; updated `acf.h`'s own doc comment to retract its prior (now-contradicted) reasoning about what the fixed 3-quadlet region already included. `REQ-RMAP-069`'s title/text/citation corrected to match. Golden-vector test `test_effective_register_write_length_helper_matches_the_formula`'s expected result changes from 1 to 0 for its existing 5-octet fixture (a real, concrete behavior change, not just an edge case). New boundary-case assertion added to `test_reg_write_len_matches_the_formula` proving the "-2" changes a real (non-zero-either-way) result. Mutation-tested: reverting the "-2" term alone reproduces both failures exactly (`Expected 4 Was 6`, `Expected 0 Was 1`), confirming the tests catch its removal; reverted and re-verified clean.

65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.226.0 -- 2026-08-11

Full-catalog audit follow-up, batch 27 (Group K, misc triage, issue #256): two items. (1) **Real bug fix**: `fragment.h`'s own file header and `RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS` macro comment claimed `segment_num` is "one octet wide... giving 256 distinct values (0..255)" -- but `acf.h`'s own documented bit layout (`read_size_or_segment_num[11:8]` in byte 6 bits 3:0, `[7:0]` in byte 7 bits 7:0) is unambiguously 12 bits wide (0..4095), independently confirmed by `src/acf.c`'s own already-tested `0x0Fu`/`0xFFu` bit-masking split in `rcp_acf_pack_header()`/`_unpack_header()`. Worse than a stale comment: `segment_num` was stored as `uint8_t` throughout `fragment.h`/`fragment.c` (`rcp_fragment_segment_t.segment_num`, `rcp_fragment_reassembler_t.expected_segment_num`, and the `rcp_fragment_reassembler_feed()` parameter), silently truncating any real decoded value above 255 -- a genuine wire-compatibility bug, not just an artificially low capacity ceiling. Fixed: widened all three to `uint16_t`, corrected `RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS` from `256u` to `4096u`, updated every doc comment describing the field's width. `REQ-FRAG-002`/`003`/`004`/`005`/`006` are phrased generically against the constant's name, not a hardcoded width, so none needed a `.fusa-reqs.json` change -- only the code and its comments were wrong. New test `test_plan_segment_num_above_255_does_not_truncate` proves segment numbers above 255 now round-trip correctly; two boundary tests (`test_plan_count_too_many_segments`, `test_plan_count_exactly_at_max_intermediate_boundary`) rewritten to reference the symbolic constant instead of hardcoded 256/257/258 literals. Mutation-tested twice: reverting `segment_num` alone to `uint8_t` produces a clean, deterministic `Expected 298 Was 42` assertion failure (298 & 0xFF); separately loosening the `rcp_fragment_plan_count()` bounds check by 4 produces an immediate SIGSEGV (a real stack-buffer overrun in a single-element test buffer) -- both confirm the fix and its guard are load-bearing, both reverted and re-verified clean. (2) **False positive, verified no fix needed**: `REQ-SCHED-001..008`, flagged by the original Group K text as possibly "handed to the RELAY-generic sanity-sweep cluster" needing scope correction. All 8 already have `scope: "tc18"` with substantive, individually-verified-accurate TC18 citations, created in one commit (v0.69.0) and never modified since -- the finding did not match the current `.fusa-reqs.json` state. Per this session's new standing rule ([[feedback_verify_against_pdf_and_reqs_json]]), the `segment_num` bit-width was additionally corroborated directly against the rendered TC18 PDF page for `acf.h`'s byte_message_info figure; the page's own resolution was too low to count exact bit-column boundaries pixel-by-pixel, so the fallback method (corroboration via `acf.c`'s own already-tested bit-masking constants) was used instead, as the rule's own text anticipates. 65/65 both trees (native + ASan/UBSan). `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100% (same pre-existing UART dangling-tag-reference quirk, non-blocking, exit 0). This closes Group K and, with it, Groups H through K of issue #256's 156-finding catalog in full.

### v0.225.0 -- 2026-08-11 (doc-only)

Full-catalog audit follow-up, batch 26 (Group J, citation-precision, doc-only, no code behavior change): with Group I's planned item list complete, moved to Group J's ~20 low-risk citation-precision findings, verifying every entry directly against TC18.txt before touching it (same discipline that already caught Group A's 17 and Group C's 16 false positives). Real fixes found and corrected: (1) two genuinely dangling `//cfusa:req REQ-SPI-031`/`032` file-level tags in `ep_spi.h`, deleted from `.fusa-reqs.json` back in v0.111.0 (superseded by generic compound-wait dispatch) but never removed from the header's own tag block -- removed. (2) A stale-identifier sweep expanded from the issue's own 5 named examples to its full actual scope: 30 `.fusa-reqs.json` entries across 12 endpoint-type modules cited dead `RCP_SERVER_DISCOVERY_BYTE_BUS_ID`/`RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED`/`RCP_SERVER_LIFECYCLE_HW_CONFIGURED`/`RCP_SERVER_LIFECYCLE_RCP_CONFIGURED`/`RCP_SERVER_FIELD_FUNCTIONAL_W` identifiers that no longer exist anywhere in this codebase -- corrected to their real, current `RCP_LIFECYCLE_*` names (confirmed `RCP_SERVER_ADMIT_*`/`RCP_SERVER_MAX_PENDING` are a *different*, still-valid identifier family, excluded from the sweep after verification). (3) Nine imprecise/wrong-section/off-by-a-few-lines citations corrected across `REQ-UART-012`, `REQ-ADC-007`/`008`/`020`/`022`, `REQ-SPI-009`/`022`, `REQ-MDIO-021`, `REQ-DISC-011`. (4) `REQ-ADC-018`'s citation was entirely mismatched -- quoted unrelated averaging-interval prose instead of the §12.3.1.3 W*-marker authorization basis its own sibling `REQ-ADC-019` already correctly cross-references it against -- corrected to match. Eight entries (`REQ-ACF-012`, `REQ-LINEP-015`, `REQ-FRAG-003`/`006`, `REQ-MOCK-012`/`013`, `REQ-CFG-003`, `REQ-WDG-002`) were verified as false positives in the original Group J list: an empty `tc18` citation is this codebase's own established, correct convention for pure internal-API-contract requirements with no TC18 basis (confirmed against many sibling `strerror()`-distinctness requirements across every endpoint type). `REQ-CANEP-003`, `REQ-WIREERR-001`, `REQ-SEQ-001`/`014`, `REQ-PWRMODE-003`/`004`/`019`, `REQ-DISC-002`/`006`/`010`/`012`/`026`, `REQ-RMAP-002`/`003` verified accurate, no change needed. 65/65 both trees. `cfusa check`: 0 errors. `cfusa trace --gaps`: 0/1024 untested; `--req-coverage 100`/`--sec-tested 100`: both 100%.

### v0.224.0 -- 2026-08-11

Full-catalog audit follow-up, batch 25 (Group I, real fix — REQ-MDIO-020/023): MDIO's own EP_func register block was unreachable from evt[2:0]=111b -- `rcp_ep_mdio_decode_read_request()`/`_decode_write_request()` already correctly rejected it, but no counterpart implemented that §12.7.1 path. A genuine, fifth instance of the address-collision editorial defect this audit keeps finding: TC18 §13.7.13.2 Table 56's own `mdio_ep_status` is printed at relative address 0x0002, colliding with `mdio_ep_enable&clr`; corrected to 0x0004 (the next unclaimed offset after the common options octet) since Table 56, unlike every other endpoint type's own table, defines no `base_clk` row at all -- `RCP_EP_MDIO_EP_FUNC_LEN` = 0x0006, one register width narrower than the common case. Fixed: `rcp_ep_mdio_functional_cfg_t` gained `ep_status` (the only field this module adds); a real TC18 Table 56 EP_func register block (`rcp_ep_mdio_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) now implements the same generic addressed-write mechanism every other endpoint type already had -- MDIO is now 9 of 11 endpoint types with it. `REQ-MDIO-020` (the already-tracked, honest not-implemented finding) moves to `implemented`, reusing its existing id; new `REQ-MDIO-023` tracks the mechanism itself. Also investigated this batch: wakeup's own Table 36 is structurally different from every other endpoint type's common-prefix layout -- a variable-length repeating IO-pin-config array with its own literal address collision, and this exact endpoint type's extraction reliability was already flagged as a limiting factor once before (Group E's own SleepCMD fix) -- deferred pending its own dedicated investigation session, matching the CAN/MDIO(Group D) precedent. `REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly (9 endpoint types now covered; CAN and wakeup remain, both deliberately deferred). Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, `index 6 out of bounds`, aborting immediately -- the same class of confirmation every prior register-block batch this session has produced). See `ROADMAP.md` for full detail.

### v0.223.0 -- 2026-08-11

Full-catalog audit follow-up, batch 24 (Group I, real fix — REQ-ISELED-026/027/029, plus retirement of a stale duplicate REQ-ISELED-028): TC18 §13.7.12.2 Table 55's ISELED functional-configuration register block was unreachable from evt[2:0]=111b -- `rcp_ep_iseled_decode_command_request()` already correctly rejected it, but no counterpart implemented that §12.7.1 path. A genuine, fourth instance of the address-collision editorial defect this audit keeps finding: Table 55's own `iseled_base_clk` is printed at relative address 0x0001 (colliding with `iseled_ep_enable&clr` at 0x0002, and omitting the reserved octet at 0x0001 every other endpoint type's table prints) -- resolved via the same cross-table structural pattern already used for PWM_OUT/GPIO/I2C: `iseled_base_clk` moves to 0x0004-0x0005, shifting every later field down by three octets. Fixed: `rcp_ep_iseled_functional_cfg_t` gained `base_clk`/`ep_status`/`wire_clk_divider`/`collect_resp`/`nr_leds`/`rcv_timeout`; a real TC18 Table 55 EP_func register block (`rcp_ep_iseled_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO/SPI/I2C/UART/LIN/ADC/PWM_IN already had -- ISELED is now 8 of 11 endpoint types with it. `iseled_use_rcv_clk` is reused directly for the block's own flags bit (already the exact wire bit); `iseled_bit_clk_divider` stays deliberately distinct from the new `wire_clk_divider`; `iseled_crc_enable` (this module's own second, independent CRC-8 layer) is deliberately NOT part of the block. `REQ-ISELED-026`/`REQ-ISELED-027` (previously honest not-implemented struct-field gaps) move to `implemented`; `REQ-ISELED-025` (the response-*aggregation behavior* itself) remains a distinct, deeper, still-open gap. Also found and fixed while scoping this batch: `REQ-ISELED-028` was a stale duplicate of the already-fixed `REQ-ISELED-007` inverted-polarity bug (Group G, PR #270) — never retired after the fix landed, the same class of catalog drift as `REQ-RMAP-046` (Group H) — now retired with a text-only correction. `REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly (8 endpoint types now covered; CAN/MDIO/wakeup remain, CAN deliberately deferred). Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, `index 14 out of bounds`, aborting immediately -- the same class of confirmation every prior register-block batch this session has produced). See `ROADMAP.md` for full detail.

### v0.222.0 -- 2026-08-11

Full-catalog audit follow-up, batch 23 (Group I, real fix — REQ-PWM-058/059, a previously-uncounted endpoint type): while scoping the next Group I candidate, found PWM_IN had been silently omitted from every prior batch's own "N of 11 endpoint types" accounting — REQ-CFG-011/REQ-CFG-012 marked the `pwm.c` module as fully done once PWM_OUT's own register-block fix landed (an earlier milestone, before this audit), without ever separately checking whether PWM_IN — a functionally distinct endpoint type sharing that source file, with its own TC18 §13.7.6.2 Table 45 — needed the identical fix. It did, and worse than any prior Group I finding: `rcp_ep_pwm_in_decode_read_request()` never checked `evt[2:0]` at all (no `rcp_acf_evt_row2_is_plain()` call, no `BAD_EVT` error code), so a real `evt=111b` configuration-write request from a conforming peer would have been silently misinterpreted as an ordinary read — a live conformance bug, not merely an unreachable path like every prior batch's own finding. Fixed both: added `RCP_EP_PWM_IN_ERR_BAD_EVT` and the missing evt check (`REQ-PWM-059`, new); implemented Table 45's clean, ten-entry register block (`rcp_ep_pwm_in_functional_cfg_t` gains `ep_status`/`clk_divider`/`flags`/`max_period`/`base_clk`; new `rcp_ep_pwm_in_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) closing the already-tracked `REQ-PWM-058` (moved `not-implemented` → `implemented`, not duplicated under a new id). `REQ-CFG-011`/`REQ-CFG-012` corrected to credit PWM_IN alongside PWM_OUT (headline count unchanged at 7/11, since `pwm.c` was already counted as one slot — this batch corrects what "PWM done" means, not the count). Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, `index 12 out of bounds`, silent SIGABRT — the same class of confirmation every prior register-block batch this session has produced). See `ROADMAP.md` for full detail.

### v0.221.0 -- 2026-08-11

Full-catalog audit follow-up, batch 22 (Group I, real fix — REQ-ADC-035/REQ-ADC-036, plus a new REQ-ADC-040): TC18 §13.7.9.2 Table 51's whole functional-configuration register block was unreachable from evt[2:0]=111b -- `rcp_ep_adc_decode_read_request()` already correctly rejected it, but no counterpart implemented that §12.7.1 path. Table 51 has no address-collision editorial defect. Unlike every prior register-block batch this session (SPI/GPIO/I2C/UART/LIN), this fix deliberately did NOT add new, parallel fields for the sampling-pipeline registers: `adc_samples_per_avg_interval`, `adc_avg_intervals_per_request`, and `adc_combine_avg_values` already share Table 51's own register names exactly, and this catalog's own prior text already treated them as the same underlying quantity, so the existing `uint16_t`/`uint8_t` fields are reused directly by `rcp_ep_adc_render_registers()`/`_apply_reconfig()` -- with an honestly-documented 8-bit truncation on render for the two wider fields, since their setters apply no range validation. Fixed: `rcp_ep_adc_functional_cfg_t` gained `ep_status`/`base_clk_divider`/`sample_interval`/`resolution`/`trigger_min`/`trigger_max`; a real TC18 Table 51 EP_func register block (`rcp_ep_adc_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO/SPI/I2C/UART/LIN already had -- ADC is now 7 of 11 endpoint types with it. `REQ-ADC-035`/`REQ-ADC-036` move to `implemented`; a new `REQ-ADC-040` (resolution/trigger_min/trigger_max, TC18.txt L5114-5122) is tracked and implemented alongside, rather than folded into 035/036, since it covers registers those two entries' own citations never named. `REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly (7 endpoint types now covered: PWM_OUT, GPIO, SPI, I2C, UART, LIN, ADC; CAN was investigated this batch and deliberately deferred -- Table 53 has a genuine acceptance/receive-filter address collision AND three opaque 32-bit bit-timing registers with no TC18-given sub-field layout, needing its own dedicated investigation session, matching the MDIO precedent). Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately -- the same class of confirmation every prior register-block batch this session has produced). See `ROADMAP.md` for full detail.

### v0.220.0 -- 2026-08-11

Full-catalog audit follow-up, batch 21 (Group I, real fix — REQ-LINEP-024): TC18 §13.7.10.2 Table 52's whole functional-configuration register block was unreachable from evt[2:0]=111b -- `rcp_ep_lin_decode_command_request()` already correctly rejected it, but no counterpart implemented that §12.7.1 path. Table 52 has no address-collision editorial defect. Fixed: `rcp_ep_lin_functional_cfg_t` gained `ep_status` and a new, distinct `wire_clk_divider` (uint8_t) field for the real 8-bit wire register, kept separate from the pre-existing, uint32_t `lin_clk_divider` (this module's own original, unit-unspecified design, matching SPI's own non-wire `clock_divider` shape) -- this session's own prior catalog text had imprecisely credited that existing field with already covering the wire register; a real TC18 Table 52 EP_func register block (`rcp_ep_lin_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO/SPI/I2C/UART already had -- LIN is now 6 of 11 endpoint types with it. Also corrects the record: the UART batch (v0.219.0) claimed `REQ-CFG-011`/`REQ-CFG-012` were narrowed to include UART, but that `.fusa-reqs.json` edit was never actually made at the time -- this batch's edit is the first to genuinely credit both UART and LIN. Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately -- the same class of confirmation every prior register-block batch this session has produced). See `ROADMAP.md` for full detail.

### v0.219.0 -- 2026-08-11

Full-catalog audit follow-up, batch 20 (Group I, real fix — REQ-UART-038, plus REQ-UART-037): TC18 §13.7.8.2 Table 48's whole functional-configuration register block was unreachable from evt[2:0]=111b -- `rcp_ep_uart_decode_write_request()`/`_decode_read_request()` already correctly rejected it (`RCP_EP_UART_ERR_BAD_EVT`), but no counterpart implemented that §12.7.1 path. Unlike GPIO's/I2C's own source tables, Table 48 has no address-collision editorial defect. Fixed: `rcp_ep_uart_functional_cfg_t` gained `ep_status`/`rts_enable`/`cts_enable`/`half_duplex`/`trail` plus two new, deliberately-separate wire-unit fields (`baud_rate_kbps` kbit/s, `wire_timeout_bit_times` bit-times) kept distinct from the pre-existing, differently-unitted `baud_rate`/`uart_timeout_ms`; a real TC18 Table 48 EP_func register block (`rcp_ep_uart_render_registers()`/`_apply_reconfig()`/`_reconfig_strerror()`/`_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO/SPI/I2C already had -- UART is now 5 of 11 endpoint types with it. `uart_stop_bits`' half-stop-bit units round-trip through the pre-existing two-valued enum via a documented, deliberately lossy mapping (1.5 stop bits rounds up to TWO on parse, the honest residual limitation). This same fix also closes the separately-tracked REQ-UART-037 (unit divergence), since the new fields carry TC18's own units directly. Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately -- the same class of confirmation SPI's/GPIO's/I2C's own equivalent checks received in v0.216.0-v0.218.0). See `ROADMAP.md` for full detail.

### v0.218.0 -- 2026-08-11

Full-catalog audit follow-up, batch 19 (Group I, real fix — REQ-I2C-019): I2C's own TC18 §13.7.7.2 Table 46 functional-configuration register block was modeled only as a bare `i2c_mode` field, with no wire render/parse path at all -- `rcp_ep_i2c_decode_transfer_request()` already correctly rejected evt[2:0]=111b as not a plain transfer (via acf.h's `rcp_acf_evt_row2_is_plain()`), but no counterpart implemented that §12.7.1 configuration-write path. A genuine, third editorial defect in a TC18 source table (visually confirmed on the PDF, not an extraction artifact): Table 46's own printed addresses collide `i2c_ep_enable&clr` with `i2c_base_clk` at 0x0002, and `i2c_base_clk`'s second octet with `i2c_ep_status` at 0x0004 -- resolved via the same cross-table structural pattern already used for `ep_pwm.h`'s EP_LEN defect and `ep_gpio.h`'s debounce-address defect: PWM_OUT's/GPIO's/SPI's own common EP_func prefixes all place EP_LEN/reserved-or-count/enable&clr/options/a 16-bit base_clk at the identical address sequence, so `i2c_base_clk` moves to 0x0004-0x0005, pushing `i2c_ep_status` to 0x0006-0x0007, `i2c_clock_divider` to 0x0008, `i2c_mode` to 0x0009, and `i2c_trail` to 0x000A (`RCP_EP_I2C_EP_FUNC_LEN` = 0x000B). Fixed: `rcp_ep_i2c_functional_cfg_t` gained `ep_status`/`clock_divider`/`trail`; a real TC18 Table 46 EP_func register block (`rcp_ep_i2c_render_registers()`/`rcp_ep_i2c_apply_reconfig()`/`rcp_ep_i2c_reconfig_strerror()`/`rcp_ep_i2c_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO/SPI already had -- I2C is now 4 of 11 endpoint types with it (`REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly, 7 endpoint types remaining). Also closed alongside, since REQ-I2C-019 already bundled it: Table 46's own Ultra-fast preset (`i2c_mode` value 4, 5 Mbit/s) was previously rejected outright by `rcp_ep_i2c_mode_valid()`; a new `RCP_EP_I2C_MODE_ULTRA_FAST` is now accepted (the separate, still-open High-speed numbering ambiguity is untouched). REQ-I2C-019 moves from `partial` to `implemented`. Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately -- the same class of confirmation SPI's and GPIO's own equivalent checks received in v0.216.0/v0.217.0). See `ROADMAP.md` for full detail.

### v0.217.0 -- 2026-08-11

Full-catalog audit follow-up, batch 18 (Group I, real fix — REQ-SPI-035): SPI's own TC18 §13.5 Table 30 row is a *third*, distinct evt[2:0] grouping, separate from both PWM_OUT/GPIO's write-semantics group and the ADC/PWM_IN/I2C/LIN/CAN/UART/ISELED/MDIO all-reserved group -- evt[2:0] 000b-101b selects one of six pre-configured channels (this module's existing design here was already correct, not a deviation), 110b is reserved, and 111b carries the same generic §12.7.1 EP_func addressed-configuration-write mechanism PWM_OUT and GPIO already implement. Until now SPI had no counterpart at all: `rcp_ep_spi_channel_valid()` rejected evt=6 and evt=7 identically as `RCP_EP_SPI_ERR_BAD_CHANNEL`, so an evt=111b request had no path through this module and TC18 Table 39's own SPI functional-configuration register block was reachable nowhere in the API, even though `rcp_ep_spi_channel_cfg_t` already stored most of the same information in a different, non-wire-mapped shape (REQ-SPI-035's previously-recorded "modeled only in reduced form" gap). Fixed: `rcp_ep_spi_channel_cfg_t` gained `baud_rate_kbps`/`use_common_cs`/`cs_clk_leadtime`/`clk_cs_trailtime`/`bits_max`/`pause_min`, `rcp_ep_spi_functional_cfg_t` gained `ep_status`, and a real TC18 Table 39 EP_func register block (`rcp_ep_spi_render_registers()`/`rcp_ep_spi_apply_reconfig()`/`rcp_ep_spi_reconfig_strerror()`/`rcp_ep_spi_encode_reconfig_request()`) now implements the same generic addressed-write mechanism PWM_OUT/GPIO already had -- SPI is now 3 of 11 endpoint types with it (`REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly, 8 endpoint types remaining). CPOL/CPHA continue to round-trip losslessly through the pre-existing `mode` byte rather than needing two new fields. Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately -- the same class of confirmation GPIO's own equivalent check received in v0.216.0). REQ-SPI-035 moves from `partial` to `implemented` -- unlike GPIO's own fix, every field REQ-SPI-035's citation flagged as missing is now modeled. See `ROADMAP.md` for full detail.

### v0.216.0 -- 2026-08-11

Full-catalog audit follow-up, batch 17 (Group G, real fix — REQ-GPIO-013): `rcp_ep_gpio_apply_reconfig()` (evt[2:0]=111b) implemented an invented pin-direction-toggle bitmask mechanism, already honestly labelled "this module's own original design" in its own file header — but corresponding to no TC18 register or mechanism at all (Table 41 has no per-pin direction field; direction lives in HW_config's separate `hw_pin_type`). This was a real, previously-diagnosed-but-uncorrected bug (flagged during Phase 5d batch 29, never fixed until now): a conformant peer's real §12.7.1 configuration request would have been silently misinterpreted. Fixed: `rcp_ep_gpio_functional_cfg_t` gained `ep_status`/`clk_divider`/`debounce[32]`, and a real TC18 Table 41 EP_func register block (`rcp_ep_gpio_render_registers()`/`rcp_ep_gpio_apply_reconfig()`/`rcp_ep_gpio_reconfig_strerror()`/`rcp_ep_gpio_encode_reconfig_request()`) now implements the same generic addressed-write mechanism `ep_pwm.c`'s PWM_OUT already had — GPIO is now 2 of 11 endpoint types with it (`REQ-CFG-011`/`REQ-CFG-012` narrowed accordingly). The old invented behavior is retained as the honestly-named `rcp_ep_gpio_toggle_pin_direction()`, no longer reachable from evt=111b. A genuine spec-table editorial defect resolved along the way (Table 41's own elided-row label for `gpio_debounce_IO31` reads 0x0024, arithmetically inconsistent with its own explicit starting pattern; the arithmetically-consistent 0x0028 was used instead, matching the exact resolution method `ep_pwm.h`'s own analogous EP_LEN defect already established). Mutation-tested (a loosened out-of-range bounds check reproduced a real stack-buffer overflow, aborting immediately — strong confirmation the original check is safety-critical, not just test-satisfying). Issue #256 Group G now fully closed (2/2: REQ-ISELED-007 + REQ-GPIO-013). See `ROADMAP.md` for full detail.

### v0.215.0 -- 2026-08-10

Full-catalog audit follow-up, batch 16 (Group I, 3 more items, extraction-only): three genuine, verified-against-primary-source gaps added as honest `.fusa-reqs.json` entries -- `REQ-LIFECYCLE-038` (`RCP_CFG_INCONSISTENT`'s own third plausibility bullet, "every configured stream has at least one endpoint using it", entirely unchecked by `rcp_lifecycle_check_rcp_cfg()`); `REQ-ADC-037` (the request/response cadence scheduling `adc_combine_avg_values` vs. `adc_avg_intervals_per_request` implies -- already honestly discussed in `ep_adc.h`'s own file header prose, but never formally tracked); `REQ-UART-038` (four Table 48 register fields -- `uart_rts_enable`/`uart_cts_enable`/`uart_half_duplex`/`uart_trail` -- with no field, setter, or round-trip of any kind). No code change (extraction work); deviation-pin tests added for each. Issue #256 Group I now 4/~10 items addressed. See `ROADMAP.md` for full detail.

### v0.214.0 -- 2026-08-10

Full-catalog audit follow-up, batch 15 (Group I, partial): added `rcp_e2e_crc_error_should_enter_safe_state()` -- TC18 §12.7.7 Table 22's own rx_enforce_e2e description names two consequences for its 1b value in the same sentence ("stream is blocked until released... Safe state will be entered"), but only the first (the stream-fault latch) had a primitive at all; the second was entirely uncited and unimplemented. New pure decision function added, mirroring the existing `rcp_e2e_overflow_should_enter_safe_state()` precedent exactly, with the same honest "pure primitive exists, cross-endpoint escalation orchestration does not yet" gap disclosure. New `REQ-E2E-045` (status: partial). Mutation-tested. Issue #256 Group I (~10 items) begun; this closes 1 of them. See `ROADMAP.md` for full detail.

### v0.213.0 -- 2026-08-10

Full-catalog audit follow-up, batch 14 (Group G, real fix): `rcp_ep_iseled_requires_isp_n()` had its polarity backwards -- TC18 Table 55 documents `iseled_use_rcv_clk` itself as "Use clock provided by ISELED 1st device instead of FreqSync pattern", meaning true selects the device-provided clock (which arrives on ISP_N), not the Freq_Sync self-recovery mode this codebase's own docs, code, and requirement entry all previously (and consistently) claimed. A pinned deviation test already named and explained this exact bug (`test_iseled_requires_isp_n_polarity_is_inverted`, added in an earlier audit pass but never acted on) -- issue #256's Group G confirmed it was still uncorrected and still accepted without a gap flag. Fixed the function body, `ep_iseled.h`'s file header, `REQ-ISELED-007`, and the two affected tests; the now-obsolete deviation-pinning test removed. Mutation-tested (revert → confirm failure → restore). Issue #256 Group G now fully closed. See `ROADMAP.md` for full detail.

### v0.212.0 -- 2026-08-10

Full-catalog audit follow-up, batch 13 (Group C, no code change): the PWM_OUT/PWM_IN/SPI `trigger` field and LIN's own trigger concept were flagged as an invented single-selectable field where TC18 defines independent signals. Primary-source read of TC18 Tables 38/42/44 found something more specific: each table names *fixed, always-on hardware trigger signals* with no client-configurable register at all (PWM_OUT's own Table 43 and SPI's own Table 39 define no trigger-select field), and none of `trigger`/`bit_order` is ever wire-serialized in this codebase (no register render/parse path touches them for any of the three modules). This module's own single-select-plus-NONE `trigger` abstraction, and SPI's `bit_order` field (found during this investigation to have no TC18 counterpart at all -- REQ-SPI-016's citation, now corrected, previously mis-cited it against unrelated Table 39 fields), are original, non-wire-affecting simplifications -- not conformance bugs. 4 of the audit's own 16-item list (REQ-PWM-020/021/039/040) were re-checked and found already honest, needing no edit. `ep_pwm.h`/`ep_spi.h`/`ep_lin.h` file headers and the 12 entries that did need it corrected to state this honestly. Issue #256 Group C now fully closed (16/16). See `ROADMAP.md` for full detail.

### v0.211.0 -- 2026-08-10

Full-catalog audit follow-up, batch 12 (Group E, real fix): the WakeUp endpoint's SleepCMD request wire codec dropped its invented 2-byte (opcode + target_mode) payload for the TC18 §13.7.2.3 Figure 22-conformant 1-byte (opcode only) form, confirmed directly against the rendered PDF page image -- SleepCMD now unconditionally means Sleep, closing a real interop-breaking bug (a genuinely conformant peer's own request would previously have been rejected as RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE, a code now retired). `rcp_powerstate_manager_encode_entry_request()` correspondingly now honestly fails for a `RCP_PWRMODE_STANDBY` target (no wire encoding exists for it) instead of silently routing it through a wire message that only ever means Sleep. REQ-WAKEUP-010/011/REQ-PWR-001 rewritten as genuine fixes; REQ-WAKEUP-012/013 (the response side, still original design, TC18 defines no response format at all) left unchanged. Issue #256 Group E now fully closed (4/4). See `ROADMAP.md` for full detail.

### v0.210.0 -- 2026-08-10

Full-catalog audit follow-up, batch 11 (Group D, no code change): MDIO's 16 findings closed by honest citation/cross-reference correction, not a wire-format rewrite. Issue #256 Group D now fully closed. See `ROADMAP.md` for full detail.

### v0.209.0 -- 2026-08-10

Full-catalog audit follow-up, batch 10 (Group A resolution, no code change): the audit's own "evt[2:0]=000b is illegal" findings refuted by TC18's own Figure 33 worked example (REQ-ACF-023 citation strengthened). Issue #256 Group A now fully closed (17/17, resolved without a fix). See `ROADMAP.md` for full detail.

### v0.208.0 -- 2026-08-10

Full-catalog audit follow-up, batch 9 (Group F, fully doc-only): `*_functional_cfg_writable()` "regardless of writer" staleness across 7 endpoint-type requirements (REQ-GPIO-020, REQ-SPI-012, REQ-I2C-004, REQ-UART-006, REQ-PWM-018/-037, REQ-LINEP-009). Issue #256 Group F now fully closed. See `ROADMAP.md` for full detail.

### v0.207.0 -- 2026-08-10

Full-catalog audit follow-up, batch 8 (Group B): PWM_IN's compound-wait comparisons now compare in the correct direction (REQ-PWM-049/-050/-051/-052). Issue #256 Group B now fully closed. See `ROADMAP.md` for full detail.

### v0.206.0 -- 2026-08-10

Full-catalog audit follow-up, batch 7 (Group H, final -- doc-only): REQ-SRV-013 cross-reference + REQ-E2E-021 honest gap-flag. Issue #256 Group H now fully closed. See `ROADMAP.md` for full detail.

### v0.205.0 -- 2026-08-10

Full-catalog audit follow-up, batch 6 (Group H): network wake now requires the same real handshake a pin wake does (REQ-PWR-005). Issue #256. See `ROADMAP.md` for full detail.

### v0.204.0 -- 2026-08-10

Full-catalog audit follow-up, batch 5 (Group H): unregistered byte_bus_id is now dropped silently, not answered with EP_NOT_FOUND (REQ-MOCK-030). Issue #256. See `ROADMAP.md` for full detail.

### v0.203.0 -- 2026-08-10

Full-catalog audit follow-up, batch 4 (Group H): compound requests now respect the endpoint-idle execution gate (REQ-SRV-006). Issue #256. See `ROADMAP.md` for full detail.

### v0.202.0 -- 2026-08-10

Full-catalog audit follow-up, batch 3 (Group H, doc-only): LIFECYCLE stale-text corrections (REQ-LIFECYCLE-001/-017/-018/-019/-020) + REQ-CANCEL-012 honest gap-flag. Issue #256. See `ROADMAP.md` for full detail.

### v0.201.0 -- 2026-08-10

Full-catalog audit follow-up, batch 2 (Group H): cancel-family evt[2:0]/hs/cs + reserved-byte validation (REQ-CANCEL-013/-014/-015, REQ-CMP-028/-029). Issue #256. See `ROADMAP.md` for full detail.

### v0.200.0 -- 2026-08-10

Full-catalog audit follow-up, batch 1 (Group H: REQ-RMAP-009/-046/-070). Issue #256. See `ROADMAP.md` for full detail.

### v0.199.0 -- 2026-08-10

**Phase 5d batch 29: `hw_pin_type` now matches TC18 Table 20's own bit
layout.** Issue #200. `REQ-RMAP-042`/`-043`. Consumer investigation
(continued from batch 28) confirmed HW_config's own `hw_pin_type` and
GPIO's own separate, runtime-adjustable `pin_property` (`ep_gpio.h`)
are genuinely two different registers, coupled only by sharing one set
of bit-position constants -- not a functional dependency. Decoupled
the fix: `rcp_regmap_hw_pin_map_entry_t`'s third field renamed
`pin_property` -> `hw_pin_type` with a brand-new, dedicated
`RCP_REGMAP_HW_PIN_*` constant family at Table 20's exact bit
positions; `RCP_REGMAP_PIN_PROP_*` and every one of its consumers in
`ep_gpio.c`/`ep_gpio.h` left completely untouched.
`config.h`/`config.c`'s manifest JSON parser updated to match
(`hw_pin_map` entries now use `"hw_pin_type"` with Table-20-derived
string values). `REQ-RMAP-043` ("all outputs are always also an
input") closes as a natural consequence: the output-stage field
selects one of four drive modes with no separate exclusive
INPUT/OUTPUT flag pair to toggle away from at all. **Bigger finding
reported separately, not fixed this batch**: re-reading TC18 Table 30
shows GPIO's own `evt[2:0]=111b` write semantics
(`rcp_ep_gpio_apply_reconfig()`) appears to deviate from TC18's own
generic EP_func-block-write definition (§12.7.1/Figure 18) --
`ep_pwm.h`'s own sibling implementation already gets this right,
GPIO's own doesn't. Posted to issue #200 and this session's own
project memory; needs a larger, separate fix. Mutation-tested with two
mutations (full revert -- fails to compile; isolated table-value swap
-- caught). Full suite (65/65) + ASan/UBSan clean on both trees. Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). `REQ-RMAP-042`/`-043` both move to `implemented`. See
`ROADMAP.md` milestone 199 for full detail.

### v0.198.0 -- 2026-08-10

**Phase 5d batch 28: EP_Signal_Nr enumeration now covers all eleven
endpoint types with correct per-type numbering.** Issue #200. First
Group 2 (HW_config, §12.7.6) batch. `REQ-RMAP-044`/`-045`.
Primary-source verification performed first, as issue #200 itself
flags for this group's contested items -- extracted TC18 §12.7.6's
own text directly from the PDF and confirmed every existing citation
accurate; no requirement-text corrections needed. `rcp_regmap_named_
signal_t` (avtp.h) extended with 18 new values (UART/LIN/PWM_OUT+
PWM_OUTN/PWM_IN/ADC/DAC/CAN/ISELED/MDIO), matching Table 21's own
order. New `rcp_regmap_named_signal_ep_signal_nr()` converts this
enum's flat ordinal into TC18's per-type-relative wire value, restarting
at 0 for every endpoint type as Table 21 requires. Deliberately did NOT
touch `pin_property`/`hw_pin_type` (`-042`/`-043`) this batch -- unlike
this item, that one has real behavioral consumers (`ep_gpio.c`'s
pin-direction logic, `config.c`'s JSON parser) and a possibly-
duplicate second field on `rcp_ep_gpio_functional_cfg_t`, flagged for
its own dedicated investigation. Mutation-tested with two mutations
(full revert -- fails to link; isolated CAN-offset mutation -- caught).
Full suite (65/65) + ASan/UBSan clean on both trees. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). `REQ-RMAP-044`/`-045` both move to `implemented`
(content/naming fix only -- no wire encode/decode path for HW_config
exists yet). See `ROADMAP.md` milestone 198 for full detail.

### v0.197.0 -- 2026-08-10

**Phase 5d batch 27: `rcp_byte_bus_id_t` widened to the full 11-bit
wire range -- Group 3 fully complete.** Issue #200. `REQ-RMAP-053`/
`REQ-ACF-020`. Full "read every consumer" investigation performed
first (~55 files) before any code change, per this item's own
standing flag. `rcp_byte_bus_id_t` (`avtp.h`) widened `uint8_t` ->
`uint16_t`. `rcp_acf_unpack_header()`'s overflow check removed (now
provably dead code -- the wire extraction is mathematically bounded to
0x7FF); `RCP_ACF_ERR_BUS_ID_OVERFLOW` retired outright from
`rcp_acf_errc_t`. Three real consumer-side fixes found and made
alongside: `adapt.c`'s byte_bus_id-to-string buffer widened
(`char[4]` -> `char[6]`); `test_mock.c`'s own decode-failure test
given a new, still-valid stimulus (an over-declared `pad` count)
since its old one (a high byte_bus_id) no longer fails to decode; and
`recorder.c`'s own binary-export local retyped after CI's `windows-
2022 / msvc` job caught a narrowing `C4244` the first, type-name-only
grep sweep had missed (it accesses `.byte_bus_id` without ever
spelling `rcp_byte_bus_id_t` by name) -- a broader field-access-pattern
re-sweep confirmed it was the only one.
Rewrote three width/reject-pinning deviation tests into positive
round-trip tests; split one combined test that used to also cover
`REQ-ACF-018`'s own unrelated, still-open deviation. Renamed one new
test after `cfusa check` flagged a `CFUSA-CY009` substring false
positive ("deco**des_**full" matching the DES-cipher pattern).
Mutation-tested with three independent mutations (full typedef
revert; isolated decode-truncation; isolated encode-masking) -- all
caught by the same three tests from different angles. Full suite
(65/65, net +2 tests) + ASan/UBSan clean on both trees. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). `REQ-RMAP-053` and `REQ-ACF-020` both move to
`implemented`. **Group 3 (EP_ID_config) is now COMPLETE: 6/6 items.**
See `ROADMAP.md` milestone 197 for full detail.

### v0.196.0 -- 2026-08-10

**Phase 5d batch 26: multi-client and heterogeneous-shared-bus
diagnostics for EP_ID_config.** Issue #200. `REQ-RMAP-057`/`-058`:
TC18 §12.7.8 recommends an endpoint be mapped to at most one RC Client
at a time, and endpoints sharing a byte_bus_id within one request
stream share the same ep_type -- neither had a diagnostic. New
`rcp_regmap_ep_id_map_has_single_client_per_ep()` (true iff no ep_id
spans two different request_stream_index values) and
`rcp_regmap_ep_id_map_shared_bus_homogeneous()` (true iff every group
of rows sharing one (request_stream_index, byte_bus_id) has an
identical caller-supplied ep_type). Found and fixed a pre-existing
scenario-accuracy bug in the old shared deviation-pin test along the
way: its "two clients" case put both rows on the same stream (not
actually two clients) and its "shared bus" case used two different
byte_bus_id values (not actually a shared bus) -- both corrected in
the two new positive tests that replace it. Mutation-tested with three
mutations (full revert; one isolated mutation per new function) --
all caught. Full suite (65/65) + ASan/UBSan clean on both trees. Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). `REQ-RMAP-057`/`-058` both move `not-implemented` ->
`partial`. **Group 3 (EP_ID_config) now 5/6 items addressed** -- only
`REQ-RMAP-053` remains. See `ROADMAP.md` milestone 196 for full
detail.

### v0.195.0 -- 2026-08-10

**Phase 5d batch 25: EP_ID_config table's own end-of-table sentinel and
power-on default row.** Issue #200. `REQ-RMAP-054`: a
`Request_Stream_Index` of 0 is TC18's own defined end-of-table
sentinel, and the table's power-on default contents must permit EP0
access before any client writes configuration -- neither existed.
New `rcp_regmap_ep_id_map_effective_count()` scans a buffer and stops
at the first sentinel row; new `rcp_regmap_ep_id_map_row_init_default()`
supplies the default row. `rcp_regmap_ep_id_map_is_ascending()` stays
deliberately sentinel-unaware, since it's a generic ordering
diagnostic, not a table iterator -- sentinel recognition is this
batch's own separate concern. Mutation-tested with two mutations (full
revert -- fails to link; isolated off-by-one on the new function's
return value -- caught by the new test). Full suite (65/65, +2 tests)
+ ASan/UBSan clean on both trees. Fresh `cfusa check`/`trace` (0
errors, 100%/100%, three separate CI-matching invocations).
`REQ-RMAP-054` moves `not-implemented` -> `partial` (this table still
has no wire encode/decode path at all). See `ROADMAP.md` milestone 195
for full detail.

### v0.194.0 -- 2026-08-10

**Phase 5d batch 24: EP_ID_config ordering now checks the composite
(Request_Stream_Index, BBID) key.** Issue #200. Direct follow-up to
`REQ-RMAP-052` (batch 23's own new field): `rcp_regmap_ep_id_map_is_
ascending()` previously compared `byte_bus_id` alone; TC18 §12.7.8
requires ordering in the composite key, so a table that legitimately
restarts its BBID run at each new, higher request stream was reported
non-ascending. Rewrote the comparison: a `request_stream_index`
decrease is always non-ascending, an increase is always ascending
regardless of that pair's own BBID, and only an unchanged stream index
falls back to the prior strict BBID comparison. Rewrote `REQ-RMAP-056`'s
own deviation pin into a positive test; re-verified (not assumed) that
every other existing consumer of the function still holds under the
new logic. Mutation-tested with two independent mutations (full revert;
isolated removal of the early `continue`) -- both correctly caught by
the new test. Full suite (65/65) + ASan/UBSan clean on both trees.
Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three separate
CI-matching invocations). `REQ-RMAP-056` moves `partial` ->
`implemented`, fully closed. See `ROADMAP.md` milestone 194 for full
detail.

### v0.193.0 -- 2026-08-10

**Phase 5d batch 23: EP_ID_config row is now a (Request_Stream_Index,
EP_Nr, BBID) triple -- Group 3 begins.** Issue #200. First Group 3
batch. Blast-radius check found `rcp_regmap_ep_id_map_entry_t` touched
in only 4 files -- dramatically smaller than `rcp_byte_bus_id_t` (this
group's next item, ~40 files including every endpoint type), which
needs its own dedicated investigation deferred rather than rushed.
New `uint8_t request_stream_index` field, deliberately placed LAST
(TC18 puts it first at row offset 0x0000, but this struct is content
modeling only, not a wire-order layout) so existing positional-
initializer test call sites kept compiling -- made the implicit
zero-init explicit rather than relying on it silently, avoiding new
warnings. Scoped to content only: `rcp_regmap_ep_id_map_is_ascending()`
stays unaware of the new field (composite-key ordering is
`REQ-RMAP-056`'s own separate scope); its existing deviation pin
updated to set real values matching its own narrative, previously left
indeterminate. Rewrote `REQ-RMAP-052`'s own deviation pin into a
positive test. Mutation-tested: full header revert with every touched
test file kept breaks the build. Full suite (65/65) + ASan/UBSan
clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three
separate CI-matching invocations). See `ROADMAP.md` milestone 193 for
full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.192.0 -- 2026-08-10

**Phase 5d batch 22: all four optional-subsystem ptr/capacity pairs
now declared -- Group 1 COMPLETE.** Issue #200. The LAST Group 1 item.
TC18 §12.7.5 Table 18's continued part defines four further 16-bit
ptr/capacity pairs -- network interface, physical layer, time synch,
security -- none of which `rcp_regmap_general_t` declared at all.
Unlike every batch since `REQ-RMAP-033`, entirely NEW fields, not a
retype. Re-read the primary source PDF's own continuation page a
second time for this batch: confirmed Table 18's own "Absolute
address" column is genuinely blank for all eight registers -- a real
gap in TC18's own table, not an extraction failure. Inferred addresses
(`0x0030`-`0x003F`) from the visible continuation marker plus the
table's own consistent, gap-free, sequential-address convention
(verified true for every other register in Table 18 without
exception), explicitly flagged as inferred rather than directly read
throughout. Rewrote the requirement's own deviation pin into a
positive test -- unlike every other Group 1 rewrite this phase, the
whole test became the positive proof, since its prior claim was now
entirely false. Mutation-tested: full header revert with every touched
test file kept breaks the build. Full suite (65/65) + ASan/UBSan
clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three
separate CI-matching invocations). See `ROADMAP.md` milestone 192 for
full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.191.0 -- 2026-08-10

**Phase 5d batch 21: `svr_ep_functional_cfg_ptr`/`svr_sequencer_
state_ptr` now correctly sized and separately addressed.** Issue #200.
Closes out the last two `rcp_regmap_table_ref_t`-typed sub-table refs
that needed retyping -- both LONE pointers (TC18 defines no adjacent
capacity register for either), same shape as `svr_hw_cfg_ptr`
(`REQ-RMAP-033`). **With this batch, `rcp_regmap_table_ref_t` is no
longer used by any field in `rcp_regmap_general_t` at all** -- every
one of the struct's original seven sub-table refs has now been
retyped. Found a genuine second usage site in a THIRD test file
(`test_tc18_gaps_server.c`) -- the first Group 1 batch to touch a
usage site outside the two regmap test files. Also corrected a stale
cross-file doc comment in `request_sequencer.h` that had become
factually wrong (both the old field name and its "pointer/capacity
pair" description). Mutation-tested: full header revert with every
touched test file kept breaks the build. Full suite (65/65) + ASan/
UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three
separate CI-matching invocations). See `ROADMAP.md` milestone 191 for
full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.190.0 -- 2026-08-10

**Phase 5d batch 20: `svr_ep_bytebus_id_map_ptr`/`_capacity` now
correctly sized and separately addressed.** Issue #200. Straightforward
width/address fix, the class `-033`/`-034` already established --
confirmed NOT the semantic-contradiction class `-036` needed, since
the shape was already independently verified against the primary
source during that batch's own read: the capacity really is an entry
count here. Retyped `ep_id_bus_map` (`rcp_regmap_table_ref_t`) to two
correctly-sized scalar fields. Split the remaining half of batch 19's
own proactive combined-pin split into a new positive test and a
narrowed remaining one for `-038`'s still-open fields. Found and fixed
the same two "second usage site" categories every batch since 16 has
needed. Mutation-tested: full header revert with every touched test
file kept breaks the build. Full suite (65/65) + ASan/UBSan clean.
Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three separate
CI-matching invocations). See `ROADMAP.md` milestone 190 for full
detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.189.0 -- 2026-08-10

**Phase 5d batch 19: `svr_ep_generic_cfg_ptr`/`_capacity` now
correctly sized, addressed, and UNIT-correct.** Issue #200. The batch
issue #200 itself flagged for careful re-reading before fixing.
Re-verified directly against the primary-source PDF before writing
code: `svr_ep_generic_cfg_capacity` (0x0026, 16 bit) is explicitly
"Length of EP config register section in bytes." The former
`ep_generic_cfg` field (`rcp_regmap_table_ref_t`) documented its own
capacity as an ENTRY COUNT -- the exact opposite unit, a genuine
semantic contradiction, not merely a width mismatch. Retyped to two
independent scalar fields matching TC18's own names/widths, so the
unit distinction is structurally enforced rather than only documented.
The same PDF read also independently re-confirmed the shapes of every
remaining Group 1 item ahead of its own batch (`-037` through `-039`)
-- no surprises found for any. Split the largest combined deviation
pin this phase has found (three requirements, `-036`/`-037`/`-038`, in
one test) into a new positive test and a narrowed remaining one; found
and fixed the same two "second usage site" categories the prior two
batches established. Mutation-tested: full header revert with every
touched test file kept breaks the build. Full suite (65/65) + ASan/
UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three
separate CI-matching invocations). See `ROADMAP.md` milestone 189 for
full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.188.0 -- 2026-08-10

**Phase 5d batch 18: `REQ-RMAP-035` -- reserved 16-bit register at
0x0022 now explicitly modeled.** Issue #200. Second reserved-register
batch this phase, applying `REQ-RMAP-031`'s own `reserved_0x17`
precedent exactly: TC18 §12.7.5 Table 18 reserves this 16-bit
register, must read 0x00 -- already true today via generic zero-fill,
but now explicitly documented as deliberate. New `uint16_t
reserved_0x22` field, zero-initializing for free, no setter anywhere
in this codebase. Closes the still-open half of batch 14's own
proactive combined-pin split (that batch closed 0x0017, left 0x0022
deliberately open). Mutation-tested: full header revert with tests
kept breaks the build. Full suite (65/65) + ASan/UBSan clean. Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 188 for full detail. 1030
requirements (unchanged), 110 `tc18-gap` entries remaining (unchanged
-- narrowed from `not-implemented` to `partial`).

### v0.187.0 -- 2026-08-10

**Phase 5d batch 17: `REQ-RMAP-034` -- request/response stream config
registers now correctly sized and separately addressed.** Issue #200.
Same class of structural fix as v0.186.0's `REQ-RMAP-033`, doubled
across two fields: TC18 §12.7.5 Table 18 defines four separate,
non-adjacent registers for the stream-config sub-tables (`svr_request_
stream_cfg_capacity`/`svr_response_stream_cfg_capacity`, 8 bit each;
`svr_request_stream_cfg_ptr`/`svr_response_stream_cfg_ptr`, 16 bit
each), while `rcp_regmap_general_t` had collapsed each pointer/capacity
pair into one `rcp_regmap_table_ref_t`, carrying the identical
compounding problems `-033` already fixed: a 16-bit capacity able to
hold values TC18's real 8-bit registers cannot, and an offset in this
project's own unit rather than TC18's own register-map address.
Blast-radius check found zero real consumers, confirmed safe to
retype -- explicitly distinguished from this codebase's separate,
unrelated `rcp_regmap_request_stream_cfg_t`/`rcp_regmap_response_
queue_cfg_t` CONTENT structs, which share a name but are a completely
different concern. Replaced with four correctly-sized scalar fields.
Found and fixed the same two "second usage site" categories batch 16
established, doubled across two fields (a general zero-init test, and
`REQ-RMAP-039`'s own deviation pin). Mutation-tested: full header
revert with every touched test file kept breaks the build. Full suite
(65/65) + ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors,
100%/100%, three separate CI-matching invocations). See `ROADMAP.md`
milestone 187 for full detail. 1030 requirements (unchanged), 110
`tc18-gap` entries remaining (unchanged).

### v0.186.0 -- 2026-08-10

**Phase 5d batch 16: `REQ-RMAP-033` -- `svr_hw_cfg_ptr` retyped to a
bare, correctly-shaped 16-bit pointer.** Issue #200. First Group 1
batch that's a real structural change rather than a pure addition or
width/rename fix, and the first to touch a shared type
(`rcp_regmap_table_ref_t`) rather than one field alone. `svr_hw_cfg_
ptr` (TC18 §12.7.5 Table 18, 0x001A) is a LONE 16-bit pointer with no
adjacent capacity register -- HW_config's extent comes from `svr_io_
pin_count` (`REQ-RMAP-032`) instead, so the field's former shared-type
shape (32-bit "register word" offset + a spurious capacity member) was
retyped to a bare `uint16_t`, matching TC18's own width exactly.
Blast-radius check (grep across `src/*.c`) found zero real consumers,
confirming safe to retype. Found and fixed three separate existing
test call sites this touched beyond its own deviation pin: a general
zero-init test (plus an unrelated, harmless type-macro nit fixed
opportunistically while already there), and two OTHER requirements'
own deviation pins (`REQ-RMAP-039`, `REQ-RMAP-040`) that referenced
the old field shape as supporting evidence for their own separate
claims -- both claims stay true, only the field reference updated.
Mutation-tested: full header revert with every touched test file kept
breaks the build. Full suite (65/65) + ASan/UBSan clean. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 186 for full detail. 1030
requirements (unchanged), 110 `tc18-gap` entries remaining (unchanged).

### v0.185.0 -- 2026-08-10

**Phase 5d batch 15: `REQ-RMAP-032` -- `svr_io_pin_count` now
explicitly modeled.** Issue #200. New `uint16_t svr_io_pin_count`
field on `rcp_regmap_general_t` (TC18 §12.7.5 Table 18, 0x0018) --
§12.7.6's authoritative source for the HW_config table's (Table 19)
declared extent, which itself stays entirely unimplemented (Group 2's
own separate, still-open scope). Split a stale-prone combined
deviation pin (`svr_io_pin_count` + the separate `svr_hw_cfg_ptr`
mis-shaping, `REQ-RMAP-033`) into a new positive test and a narrowed
remaining one. Mutation-tested: full header revert with tests kept
breaks the build (sufficient rigor for a field with no computed
logic). Full suite (65/65, +1 test) + ASan/UBSan clean. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 185 for full detail. 1030
requirements (unchanged), 110 `tc18-gap` entries remaining (unchanged
-- narrowed from `not-implemented` to `partial`).

### v0.184.0 -- 2026-08-10

**Phase 5d batch 14: `REQ-RMAP-031` -- reserved octet at 0x0017 now
explicitly modeled.** Issue #200. Verified `rcp_discovery_encode_
response()` directly before writing code, to rule out a concern raised
by v0.183.0's `svr_implemented_options` field shrink (`uint32_t` ->
`uint8_t`, -3 octets): confirmed the encoder writes only 5 named
fields into its leading 0x000D-octet slice and zero-fills the rest of
its scratch buffer -- nothing is wire-packed by struct layout, so the
shrink could not have silently shifted any later field's documented
address. New `uint8_t reserved_0x17` field on `rcp_regmap_general_t`
(no setter anywhere in this codebase), zero-initializing for free.
Documents the octet as deliberately reserved rather than merely
absent, and gives a future wire-dispatch implementation a concrete
field to write zero from. Split a stale-prone combined deviation pin
(0x0017 + the separate, still-open 0x0022 register) into a new
positive test and a narrowed remaining one. Mutation-tested: full
header revert with tests kept breaks the build (sufficient rigor for a
field with no computed logic, matching batches 10/12's precedent).
Full suite (65/65, +1 test) + ASan/UBSan clean. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 184 for full detail. 1030
requirements (unchanged), 110 `tc18-gap` entries remaining (unchanged
-- narrowed from `not-implemented` to `partial`).

### v0.183.0 -- 2026-08-10

**Phase 5d batch 13: `REQ-RMAP-030` -- `svr_implemented_options`
fixed to Table 18's real 8-bit, five-independent-bit layout;
`REQ-RMAP-004..008` retired.** Issue #200. `-030`'s own analysis
(Table 18: 8-bit register, one independent bit per feature) directly
contradicted the already-closed `REQ-RMAP-004..008`'s 32-bit,
six-bit, three-forced-pair design (enforced by
`rcp_regmap_options_group_consistent()`, citing §12.9.1.1). Neither
side could self-verify (TC18.txt was unavailable this session);
surfaced to the user, who chose to pause and verify against the
actual specification rather than let either side win by default.
That verification located a primary source neither this session nor
its predecessor had found -- `OA_TC18_specification_v_0.5.1_RC.pdf`,
the real 117-page TC18 spec (prior searches only ever checked for
`.txt` files). Reading it directly: Table 18 (pages 51-53) confirms
`-030`'s reading exactly -- 8-bit register, "abcdefgh" with f/g/h
reserved, no pairing concept; §12.9.1.1 (page 64), `-004..008`'s
shared citation, is titled "Handling multiple requests in incoming
messages" and is entirely about an RC Server processing several
ACF-type requests packed into one AVTPDU frame -- it says nothing
about `svr_implemented_options`, feature advertisement, or any
bit-pairing rule; the exact phrase these five requirements quote does
not appear anywhere in the section. `REQ-RMAP-004..008`'s citation is
a genuine misattribution, retired outright (new `"retired"` status/
scope, empirically confirmed tolerated by `cfusa check` before
committing to the value).

`rcp_regmap_general_t.svr_implemented_options` retyped `uint32_t` ->
`uint8_t`; five independent single-bit constants replace the six
invented, paired ones; `rcp_regmap_options_group_consistent()` removed
outright. All three real consumers updated:
`rcp_timed_feature_enabled()` (single-bit check),
`cli.c`'s `capabilities_json()` "features" array (gained
"trigger"/"chained", closing a real, previously-unadvertised
capability gap -- c-RCP has always implemented both request types in
full but had no bit to advertise either), and `config.c`'s manifest
parser (gained matching names). `test_cli.c` gained a genuinely new
test inspecting "features" content -- no prior test in that file did.
`REQ-RMAP-030` stays `partial`/`tc18-gap` (0x0016 is still past the
0x000D wire-reachability ceiling, `REQ-RMAP-024`, still open) with
fully rewritten text. Mutation-tested five ways (three isolated-logic,
one full seven-file production revert with all four touched test
files' changes kept -- breaks the build, confirming real dependency).
Full suite (65/65 ctest suites, net -1 individual test case) + ASan/
UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three
separate CI-matching invocations). See `ROADMAP.md` milestone 183 for
full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged), 5 newly `retired`.

### v0.182.0 -- 2026-08-10

**Phase 5d batch 12: REQ-RMAP-029 -- svr_configuration_lock field
added, enforcement deliberately deferred to REQ-RMAP-055.** Issue
#200. First Group 1 item that is genuinely new functionality rather
than a rename/width fix, and the first requiring a cross-reference to
a DIFFERENT requirement before writing code: `-029`'s own consequence
text names real enforcement as the goal, but `-055` (issue #200 Group
3) is this codebase's own already-written "shared plumbing, implement
once" note for the exact lock-check primitive this register would
drive -- Table 23's EP_ID_config and Table 24's queue registers are
ALSO R/W+ and need the identical check. New `uint8_t
svr_configuration_lock` field (0x0015), zero-initializing to the
correct "unlocked" default; deliberately NOT wired into
`rcp_lifecycle_field_writable()` here. The existing deviation pin's
own core claim stayed entirely true (enforcement untouched), so its
assertions needed no rewrite -- only its comment narrowed, plus a new,
separate positive test for the field's own content. `REQ-RMAP-029`
moves `not-implemented` -> `partial`, its text explicitly naming
`REQ-RMAP-055` as the real-enforcement dependency. Mutation-tested:
full header revert with tests kept breaks the build (sufficient rigor
for a field with no computed logic of its own, matching batch 10's
precedent). Full suite (65/65, +1 test) + ASan/UBSan clean. Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 182 for full detail. 1030
requirements (unchanged), 110 `tc18-gap` entries remaining (unchanged).

### v0.181.0 -- 2026-08-10

**Phase 5d batch 11: REQ-RMAP-028 -- svr_sequencers_max corrected
width, synced from the real sequencer table.** Issue #200. Third
Group 1 batch to touch an existing field, but the first with a real,
already-wired `src/*.c` consumer. Renamed+retyped
`svr_max_sequencers` (`uint16_t`) -> `svr_sequencers_max` (`uint8_t`),
matching TC18's own register name and width. `mock.c`'s
`rcp_mock_server_set_sequencer_count()` -- the concrete, already-
existing composition point request_sequencer.h's own file header had
already named -- gains a sync step keeping the register equal to the
table's actual post-call count (not the caller's raw request, which
may exceed what allocation actually produced), capped at `0xFF` rather
than truncated/wrapped. Key design insight: `request_sequencer.h`
already has `rcp_sequencer_table_unsupported()` implementing TC18's
exact "0 means not supported" rule against the table's own count --
once the register is kept synced with that same count, it reflects
the rule by construction, with zero new predicate needed. Rewrote the
deviation pin into a positive test proving the sync through a real
`rcp_mock_server_t`; found and fixed a second, unrelated pin
referencing the old field name. Mutation-tested two ways (sync-line
removal fails the new test; full header revert with tests kept breaks
the build -- for the first time in Group 1, inside `src/mock.c`
itself, confirming a real consumer depends on it). Full suite (65/65)
+ ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%,
three separate CI-matching invocations). See `ROADMAP.md` milestone
181 for full detail. 1030 requirements (unchanged), 110 `tc18-gap`
entries remaining (unchanged).

### v0.180.0 -- 2026-08-10

**Phase 5d batch 10: REQ-RMAP-027 -- svr_responder_mem_size/
svr_req_mem_size distinctly addressed.** Issue #200. Second Group 1
batch replacing an existing field: the single undifferentiated 32-bit
`svr_memory_capacity` splits into two distinct 16-bit fields matching
TC18 Table 18's own two registers exactly -- `svr_responder_mem_size`
(0x0010) and `svr_req_mem_size` (0x0012), both counted in 32-bit words
on the wire (same "caller converts units" convention as
`respqueue.h`'s own `capacity_octets`/`queue_size`). Rewrote the
deviation pin into a positive test proving both fields are 2 octets
wide, independently settable, and independently zero-initialize.
`REQ-RMAP-027` moves `not-implemented` -> `partial` -- content now
correctly modeled and distinguishable, still not wire-reachable
(`REQ-RMAP-024`). Mutation-tested: full header revert with tests kept
breaks the build (the correct, sufficient rigor for a purely-additive
struct split with no computed logic to mutate). Full suite (65/65) +
ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%,
three separate CI-matching invocations). See `ROADMAP.md` milestone 180
for full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged).

### v0.179.0 -- 2026-08-10

**Phase 5d batch 9: REQ-RMAP-026 -- svr_req_stream_max/
svr_responder_streams_max corrected to 8-bit width.** Issue #200. First
Group 1 batch to retype/rename an existing field rather than only add
new ones -- checked blast radius first (one declaration, five test-only
usage sites, no `src/*.c` consumer). Retyped `uint16_t` -> `uint8_t`
(matching TC18 Table 18's own register width; a value the real register
could never hold is now impossible to construct) and renamed
`svr_max_request_streams` -> `svr_req_stream_max` to match TC18's own
register name. New `svr_responder_streams_max` (`uint8_t`) fills the
previously-missing register. Rewrote the deviation pin (which
specifically demonstrated the old 16-bit field accepting `0x0100`, now
impossible) into a positive test proving both fields are 1 octet wide
and round-trip `0xFF` correctly. `REQ-RMAP-026` stays `partial`
(unchanged status, narrowed text) -- content now correct, still not
wire-reachable (`REQ-RMAP-024`). Mutation-tested two ways (retype
reverted fails the new `sizeof()` assertion; full header revert with
tests kept breaks the build). Full suite (65/65) + ASan/UBSan clean.
Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three separate
CI-matching invocations). See `ROADMAP.md` milestone 179 for full
detail. 1030 requirements (unchanged), 110 `tc18-gap` entries remaining
(unchanged).

### v0.178.0 -- 2026-08-10

**Phase 5d batch 8: REQ-RMAP-025 -- RCP_LIFECYCLE_FIELD_READ_ONLY
classification primitive.** Issue #200. New `RCP_LIFECYCLE_FIELD_
READ_ONLY` on `rcp_lifecycle_field_kind_t` (lifecycle.h): unwritable
unconditionally, in every state, by every writer -- genuinely different
from the three existing kinds (each writable by some writer in some
state), so it gets its own explicit case rather than the switch's
defensive default. `rcp_lifecycle_field_write_error()` needed zero
changes: it has no per-kind switch of its own, just a generic
maximally-privileged-writer re-evaluation, so the new kind automatically
and correctly reports `RCP_ERROR_LOCKED_MEM_ACCESS`. REQ-RMAP-025's own
pre-existing text already named the real constraint ("the read-only
property is presently unobservable rather than enforced... when [the
wire write path] is added") -- this batch adds the classification
primitive a future wire dispatch will need, while the requirement itself
honestly moves `not-implemented` -> `partial`, matching REQ-RMAP-023/
061/065's now-familiar pattern (rcp_mock_server_regmap() still hands out
a directly mutable pointer; nothing wires the new classification to a
real write attempt yet -- REQ-RMAP-024, unchanged). Mutation-tested two
ways (flipped case value fails the new test; full production-code
revert with tests kept breaks the build). Full suite (65/65) +
ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%,
three separate CI-matching invocations). See `ROADMAP.md` milestone 178
for full detail. 1030 requirements (unchanged), 110 `tc18-gap` entries
remaining (unchanged -- narrowed from `not-implemented` to `partial`).

### v0.177.0 -- 2026-08-10

**Phase 5d batch 7: REQ-RMAP-023 -- svr_lifecycle_state register-map
field, Group 1 begins.** Issue #200. New `uint8_t svr_lifecycle_state`
field on `rcp_regmap_general_t` (content modeling only). Scoping finding
first: the only wire path into the general register map today
(`rcp_discovery_encode_response()`) is deliberately scoped to just its
leading 14-octet device-recognition slice -- REQ-RMAP-024's own separate,
still-open gap -- so Group 1's items, this one included, are about
register *content* correctness, not wire reachability; REQ-RMAP-023
moves `not-implemented` -> `partial` accordingly, matching REQ-RMAP-061/
065's own established split. `mock.h`'s `rcp_mock_server_t` (already
holding both a bare `rcp_lifecycle_state_t` and an `rcp_regmap_general_t`
side by side) is the concrete composition point: `rcp_mock_server_
transition()` gains one line keeping the new field synced with the
authoritative state on every transition, success or failure. Mutation-
tested two ways (isolated sync-line removal fails the new test alone;
full production-code revert with tests kept breaks the build). Full
suite (65/65) + ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors,
100%/100%, three separate CI-matching invocations). See `ROADMAP.md`
milestone 177 for full detail. 1030 requirements (unchanged), 110
`tc18-gap` entries remaining (unchanged -- narrowed from
`not-implemented` to `partial`, not removed).

### v0.176.0 -- 2026-08-10

**Phase 5d batch 6: REQ-RMAP-064/065 -- the Flush_time trigger and
empty-queue heartbeat composition close Group 4.** Issue #200. New
`rcp_respqueue_should_flush_by_time(elapsed_since_last_transmit_us,
flush_time_us)` on the `e2e.h` `rcp_e2e_wd_evaluate()` model (elapsed
time as a caller-supplied input, no owned clock); closes `REQ-RMAP-064`
outright. Deliberately independent of queue emptiness, so it fires the
same way whether the queue is empty or not; combined with
`rcp_respqueue_plan_batch()` already reporting 0 for an empty queue and
`avtp.h`'s `rcp_avtp_encode_ntscf()` already accepting a zero-length
payload, the empty heartbeat AVTPDU is fully constructible from
existing primitives -- proven end to end by a new positive test.
`REQ-RMAP-065` moves `not-implemented` -> `partial`: the primitive half
closes, but scheduling this composition against a real clock and a real
transport stays an integrator concern, matching this library's other
liveness modules (`watchdog.c`, `deadline.c`). Cross-cutting discovery:
a domain-term sweep found `REQ-SRV-017` (server.h, TC18 §13.7.1.1)
pinning the identical gap under a different citation -- its own
pre-existing text already stated the same scheduling-boundary language
independently, confirming `partial` (not a full close) was the right
target status; narrowed both entries' text. Mutation-tested two ways
(both logic-only). Full suite (65/65, +3 new tests) + ASan/UBSan clean.
Fresh `cfusa check`/`trace` (0 errors, 100%/100%, three separate
CI-matching invocations). See `ROADMAP.md` milestone 176 for full
detail. 1030 requirements (unchanged), 110 `tc18-gap` entries remaining
(was 111). **Group 4 (response/ack queue config, issue #200) is now
fully closed.**

### v0.175.0 -- 2026-08-10

**Phase 5d batch 5: REQ-RMAP-063 -- flush_on_count trigger + AVTPDU
packing plan.** Issue #200. Two new, purely additive functions on
`respqueue.h`'s `rcp_respqueue_t` (no existing signature changed).
`rcp_respqueue_should_flush()` implements TC18's trigger condition
against the queue's own running octet total. `rcp_respqueue_plan_batch()`
implements the packing half, reporting how many FIFO-ordered entries
fit together within one AVTPDU; a caller drains exactly that many via
the existing `rcp_respqueue_pop()` and repeats until empty.
`scheduler.h`'s decode-side `rcp_sched_split_frame_members()` needed no
changes -- this is its missing encode-side counterpart. Found and split
a THIRD pre-existing combined deviation pin this phase
(`-063`/`-064`/`-065` bundled together); `-063`'s half rewritten to a
positive conformance test. Also hit and fixed a `cfusa` false positive
(`CFUSA-CY009`'s naive substring match on `des_` inside
`includes_at_least`) by renaming the offending test identifier. Full
suite (65/65) + ASan/UBSan clean. Mutation-tested two ways (both
logic-only, since this batch added no new required parameters). Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 175 for full detail. 1030
requirements (unchanged), 111 `tc18-gap` entries remaining (was 112).

### v0.174.0 -- 2026-08-10

**Phase 5d batch 4: REQ-RMAP-061/062 -- Max_AVTPDUsize transmit
enforcement + fragmentation budget.** Issue #200. `respqueue.h`'s
`rcp_respqueue_t` gains a second, independent ceiling alongside `-059`'s
aggregate capacity: `max_avtpdu_size_octets` (TC18 §12.7.9 Table 24).
`rcp_respqueue_init()` gains a required third parameter;
`rcp_respqueue_push()` refuses (queue unchanged) any single frame whose
own length exceeds it -- `REQ-RMAP-061`'s transmit-enforcement half
(the MTU-consistency-check and discovery-exposure halves remain open;
`-061` stays `partial`). New function
`rcp_respqueue_max_fragment_payload()` closes `REQ-RMAP-062`: a pure
helper deriving the correct budget for `fragment.h`'s existing,
unchanged `rcp_fragment_plan()` mechanism, conservatively reserving the
fixed ACF header plus worst-case trailing pad. Found and rewrote TWO
more pre-existing deviation pins this batch's grep sweep surfaced
(one predating this phase entirely) -- reinforces last batch's lesson
that a domain-term grep across the whole suite, not just the file a
citation points at, is needed before declaring a sweep complete.
Mutation-tested THREE ways (full revert breaks the build; an isolated
per-message-ceiling removal fails 2 tests; an isolated pad-reservation
removal fails 3 tests across both files). Full suite (65/65) +
ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors, 100%/100%,
three separate CI-matching invocations). See `ROADMAP.md` milestone 174
for full detail. 1030 requirements (unchanged), 112 `tc18-gap` entries
remaining (was 113).

### v0.173.0 -- 2026-08-10

**Phase 5d batch 3: `respqueue.h`/`respqueue.c` -- the response/ack
transmit queue.** Issue #200. Group 4's foundational item: this
codebase had NO transmit queue for responses/acknowledges anywhere
before this batch (`server.h`'s own queue holds inbound requests, a
structurally different concept). New module `rcp_respqueue_t` -- a FIFO
of framed byte messages mirroring `server.h`'s own
init/push/pop/ownership conventions, with one TC18-mandated
behavioral difference: capacity is enforced as an OCTET budget (TC18
§12.7.9 Table 24's `queue_size` is a memory reservation, "assigned
memory in 32bit words," not a message-count cap), refusing a push that
would exceed it and leaving the queue unchanged. `regmap.h`'s
`rcp_regmap_response_queue_cfg_t` gains the `queue_size` register
itself. New test file `test_respqueue.c` (6 tests), registered in both
`CMakeLists.txt` and `tests/CMakeLists.txt` -- the first Phase 5c/5d
batch to add a brand-new source file rather than edit existing ones.
Mutation-tested two ways: temporarily dropping the new source from the
build breaks the BUILD with a link error; an isolated capacity-check
removal leaves the build green but fails both the dedicated capacity
test and the rewritten conformance test in `test_tc18_gaps_regmap.c`.
Full suite (65/65) + ASan/UBSan clean. Fresh `cfusa check`/`trace` (0
errors, 100%/100%, three separate CI-matching invocations). See
`ROADMAP.md` milestone 173 for full detail. 1030 requirements
(unchanged), 113 `tc18-gap` entries remaining (was 114).

### v0.172.0 -- 2026-08-10

**Phase 5d batch 2: REQ-RMAP-060 -- response queue STREAM_UID
register.** Issue #200. Group 4's smallest, most self-contained item,
taken first since the group's other 6 items all require a genuinely new
transmit-queue subsystem that doesn't exist yet. `regmap.h`'s
`rcp_regmap_response_queue_cfg_t` gains a `stream_uid` field (TC18
§12.7.9 Table 24) and a new `rcp_regmap_response_queue_stream_id(cfg,
mac)` helper -- `avtp.h`'s existing `rcp_stream_id_make()` already takes
a `unique_id` argument that IS the STREAM_UID register; the gap was
that the config struct had nowhere to persist a configured value for
it. Purely additive (default 0). Found and split a pre-existing
combined `-059`/`-060` deviation pin into a closed positive test and a
still-open one, applying Phase 5c's "split before it's stale"
discipline on the first Group 4 batch. Mutation-tested (full revert
breaks the build). Full suite (64/64) + ASan/UBSan clean. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 172 for full detail. 1030
requirements (unchanged), 114 `tc18-gap` entries remaining (was 115).

### v0.171.0 -- 2026-08-10

**Phase 5d batch 1: REQ-RMAP-069 -- effective register-write payload
length helper.** Issue #200. First batch of Phase 5d (RMAP register-map
exposure gaps, chosen to go next over Phase 5e since issue #200 already
gives a concrete implementation order). The suggested warm-up: new
`acf.h` function `rcp_acf_reg_write_len(acf_msg_length, pad)` -- TC18
§13.7.1.2's "Effective number of bytes to be written = (acf_msg_length
- 3) x 4 - pad," a pure arithmetic helper alongside the existing
`rcp_acf_pad_len()`. Fail-safe on a malformed frame: returns 0, never
underflowing, for a too-small `acf_msg_length` or a `pad` exceeding
what remains. Purely additive -- no existing signature changed. Found
and rewrote a pre-existing deviation pin
(`test_effective_register_write_length_helper_absent`) that an initial
grep for the requirement id's literal string had missed (it cited the
TC18 section number in its own comment instead). Mutation-tested two
ways: full revert breaks the BUILD; a signature-preserving logic
mutation (pad ignored) fails both tests exercising a nonzero pad. Full
suite (64/64) + ASan/UBSan clean. Fresh `cfusa check`/`trace` (0
errors, 100%/100%, three separate CI-matching invocations). See
`ROADMAP.md` milestone 171 for full detail. 1030 requirements
(unchanged), 115 `tc18-gap` entries remaining (was 116).

### v0.170.0 -- 2026-08-10

**Phase 5c batch 8: REQ-PWRMODE-014/015 close Group 4 -- Phase 5c is
complete.** Issue #199. The last group (cold-start/config persistence).
Overlap-verified against LIFECYCLE's own config-locking work (issue
#198, closed in Phase 5b) first, per this phase's scoping discipline --
orthogonal concerns, no shared plumbing. `rcp_pwrmode_cold_start_
lifecycle_target()` gains a required `recovered_state` parameter (only
2 call sites, both tests) -- the caller's own already-recovered fact
(this module owns no NVM access of its own, the `network_available`
convention), returned unchanged for a valid `lifecycle.h` state,
falling back to `RCP_LIFECYCLE_HW_UNCONFIGURED` for a genuinely
unconfigured device or any unrecognized/corrupt value. Closes
`REQ-PWRMODE-014`. `REQ-PWRMODE-015` closed via documentation: the
retention guarantee TC18 requires was already provided by
`rcp_pwrmode_transition()`'s existing, correct Normal<->StandBy = HOT
classification -- `RCP_PWRMODE_START_HOT`'s own enumerator comment now
states the retention obligation explicitly. Mutation-tested two ways:
a full revert breaks the BUILD (signature coverage), and a separate
signature-preserving logic mutation is caught by exactly the one test
pinning it (branch coverage the build-break signal alone would have
missed). Full suite (64/64) + ASan/UBSan clean. Fresh `cfusa
check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 170 for full detail. 1030
requirements (unchanged), 116 `tc18-gap` entries remaining (was 118).

**Phase 5c is now complete: all 15 items across all 4 groups addressed**
(14 closed outright, 1 left honestly `partial` with a real
architecture-limit citation). Issue #199 can be closed.

### v0.169.0 -- 2026-08-10

**Phase 5c batch 7: REQ-PWRMODE-021/022/027 -- `rcp_pwrmode_commit_network_sleep()`
closes Group 3.** Issue #199. Network-level sleep coordination, scoped
explicitly per the issue's own warning before any code was written.
TC14/TC10 are PHY/MAC-level out-of-band sleep signaling, not an
RCP/ACF wire message this library's decode layer could ever parse --
the honest fix extends the `network_available` convention already
established in Groups 1-2 rather than fabricating a fake decoder. One
new function, `power.h`'s `rcp_pwrmode_commit_network_sleep(mode, gate,
response_sent, out_start_kind)`, identical to
`rcp_pwrmode_commit_entry(mode, RCP_PWRMODE_SLEEP, ...)` in every
respect except it has no `target` parameter at all -- closing
`REQ-PWRMODE-021`'s StandBy exclusivity ("StandBy can only be initiated
via request to the RC Server") by construction, not merely by omission,
and `REQ-PWRMODE-022` (network sleep initiation) because it now genuinely
admits a network-triggered Sleep entry under the identical conditions a
normal request uses. `REQ-PWRMODE-027` (LPS suppression) closed with no
new code: the function's own `RCP_PWRMODE_OK`/`RCP_PWRMODE_ERR_ENTRY_REFUSED`
return value IS the confirmation signal a real PHY integration gates
LPS assertion on. Mutation-tested: full revert breaks the BUILD. Full
suite (64/64) + ASan/UBSan clean. Fresh `cfusa check`/`trace` (0 errors,
100%/100%, three separate CI-matching invocations). See `ROADMAP.md`
milestone 169 for full detail. 1030 requirements (unchanged), 118
`tc18-gap` entries remaining (was 121) -- **Group 3 of Phase 5c is now
fully closed.**

### v0.168.0 -- 2026-08-10

**Phase 5c batch 6: REQ-PWRMODE-028 -- admission-suspend state closes
Group 2.** Issue #199. The deferred fifth item of Group 2.
`server.h`'s `rcp_server_endpoint_t` gains an additive
`admission_suspended` bool field (default `false`, every pre-existing
caller silently unaffected) and a new setter,
`rcp_server_endpoint_set_admission_suspended()`.
`rcp_server_endpoint_admit()` checks it first, before inspecting the
arriving frame at all, and returns a new outcome,
`RCP_SERVER_ADMIT_SUSPENDED` -- neither queued nor executed --
implementing TC18 §13.7.2.3 step 1 ("stop entering incoming requests
into endpoint queues") ahead of step 3's preconditions
(`rcp_pwrmode_check_entry()`/`rcp_pwrmode_commit_entry()`, batch 5).
`rcp_server_endpoint_submit()` deliberately does not consult the new
flag -- it is the lower-level primitive `admit()` is built on; a caller
wanting this admission-suspend semantics routes requests through
`admit()`. New enum value confirmed additive-safe before adding it:
`mock.c`'s `finish_admission()`, the only place in this codebase that
switches over `rcp_server_admit_t`, already has a `default:` case that
absorbs it correctly. Mutation-tested: full revert breaks the BUILD.
Full suite (64/64) + ASan/UBSan clean, no new warnings anywhere. Fresh
`cfusa check`/`trace` (0 errors, 100%/100%, three separate CI-matching
invocations). See `ROADMAP.md` milestone 168 for full detail. 1030
requirements (unchanged), 121 `tc18-gap` entries remaining (was 122) --
**Group 2 of Phase 5c (§12.5/§13.7.2.3) is now fully closed.**

### v0.167.0 -- 2026-08-10

**Phase 5c batch 5: REQ-PWRMODE-023/024/025/026 -- `rcp_pwrmode_commit_entry()`,
server-wide gate scoping, SleepCMD authorization.** Issue #199. Four of
Group 2's five items (§12.5 sleep-entry races/scope errors).
`REQ-PWRMODE-024`/`-026` closed by one new function, `power.h`'s
`rcp_pwrmode_commit_entry(mode, target, gate, response_sent,
out_start_kind)`: a caller now runs a two-step admission (check a
first-sampled gate to decide what response to send, then commit against
a FRESHLY re-sampled gate only once that response has actually been
transmitted), closing the lost-wakeup race by re-validating the gate at
the actual transition point and enforcing response-before-transition
ordering via a required `response_sent` bool (no I/O of its own, so the
caller supplies proof, mirroring `network_available`'s own precedent).
`REQ-PWRMODE-025` closed via documentation-only correction: the gate's
`endpoint_idle`/`response_queue_empty` fields were always opaque
caller-supplied bools with no code-level single-endpoint restriction --
only the pre-fix docs invited a narrow reading; now explicit that both
are server-wide aggregates. `REQ-PWRMODE-023` closed: the "entire RC
Server" concern is satisfied by construction (`commit_entry()` already
operates on one whole-server `rcp_pwrmode_t`, `rcp_powerstate_manager_t`'s
own per-peer tracking being legitimate CLIENT-side bookkeeping of
multiple different remote servers, not a gap); the authorization gap was
real and is now closed by `ep_wakeup.h`'s new
`rcp_ep_wakeup_sleepcmd_writable()`, gating SleepCMD admission on
`writer.via_root_client_ep0`. Three deviation-pin tests that had each
combined multiple requirement ids in one body were split so closed and
still-open ids each get their own clean test, avoiding the staleness
class caught in batch 4. Mutation-tested: full revert of all 4 touched
files breaks the BUILD (rewritten tests reference symbols that don't
exist pre-fix). Full suite (64/64) + ASan/UBSan clean, fresh `cfusa
check`/`trace` (0 errors, 100%/100%, all three separate CI-matching
invocations). See `ROADMAP.md` milestone 167 for full detail. 1030
requirements (unchanged), 122 `tc18-gap` entries remaining (was 126,
four genuine closures) -- Group 2 of Phase 5c is now closed except
`REQ-PWRMODE-028` (admission-suspend, deferred to a follow-on batch --
needs `server.h` admission-path surgery, out of this batch's scope).

### v0.166.0 -- 2026-08-10

**Phase 5c batch 4: REQ-PWRMODE-017/018 -- responder-stream recording
and the WakeUp termination condition.** Issue #199. Group 1's last two
items, scoped together since both trace to the same missing piece.
`REQ-PWRMODE-017`: TC18 §12.4.1 requires the wake response go out on
the responder stream *configured for* the original standby request --
genuinely a different `StreamID` than the request's own, confirmed via
`regmap.h`'s own request-stream/response-queue pairing model.
`rcp_powerstate_manager_handshake_begin()` gains a required
`resp_stream_id` parameter (no safe default exists) and a new
`rcp_powerstate_manager_wake_response_stream_id()` getter returns it
for the caller to transmit on. `REQ-PWRMODE-018` closed via
documentation/citation correction with zero new logic: the underlying
`rcp_pwrmode_handshake_wakeup_attempt()` primitive was already
sufficiently generic (a plain caller-supplied bool); only one narrow
convenience wrapper and its docs needed clarifying that a literal
WakeUp echo is one way to satisfy the handshake, not the only one.
Mutation-tested two ways: full revert breaks the build; a precise
single-line mutation isolates exactly the 2 tests pinning the
recorded-stream round-trip. Full suite (64/64) + ASan/UBSan clean,
fresh `cfusa check`/`trace` (0 errors, 100%/100%, all three separate
CI-matching invocations). See `ROADMAP.md` milestone 166 for full
detail. 1030 requirements (unchanged), 126 `tc18-gap` entries remaining
(was 128, two genuine closures) -- Group 1 of Phase 5c is now fully
closed out.

### v0.165.0 -- 2026-08-10

**Phase 5c batch 3: REQ-PWRMODE-016 -- hot start now checks network
availability before spending its WakeUp budget.** Issue #199.
`rcp_pwrmode_handshake_iface_reenabled()` previously advanced
unconditionally, with no network-availability input at all, so step
(b)'s WakeUp-message repetition could be driven immediately regardless
of whether the network was actually up -- TC18 §12.4.1 requires
enabling the interface, THEN checking network availability, and only
starting the message repetition once it is. Fixed:
`rcp_pwrmode_handshake_iface_reenabled()` gains a `network_available`
parameter; `false` leaves the handshake at `NOT_STARTED` (a cheap,
uncounted "not yet," not a failure) so the WakeUp-repeat budget is
never touched until the network is actually available.
`rcp_powerstate_manager_handshake_begin()` threads the same parameter
through. Required-parameter change touched 13 call sites across 6
files (the "compiler enumerates every site" technique) -- every
existing test represents the happy path and updated mechanically with
zero behavioral change, plus one genuinely new test pinning the gate
itself. Mutation-tested two ways: full revert breaks the build; a
precise single-line mutation isolates exactly the new test. Full suite
(64/64) + ASan/UBSan clean, fresh `cfusa check`/`trace` (0 errors,
100%/100%, all three separate CI-matching invocations). See
`ROADMAP.md` milestone 165 for full detail. 1030 requirements
(unchanged), 128 `tc18-gap` entries remaining (was 129, one genuine
closure).

### v0.164.0 -- 2026-08-10

**Phase 5c batch 2: REQ-PWRMODE-020 -- network wake now runs the same
handshake as pin wake.** Issue #199. `rcp_pwrmode_hotstart_required()`
previously returned `false` for a network wake, skipping the handshake
entirely -- this module's own file header and `REQ-PWRMODE-005`'s own
catalog text had encoded that as deliberate design, but primary-source
re-verification (TC18 §12.4.1: a network wake "will directly check for
the network availability and proceed as before") confirmed it was
wrong -- "proceed as before" means run the same procedure a pin wake
does, not skip it. Fixed: `rcp_pwrmode_hotstart_required()` now returns
`true` unconditionally; `rcp_pwrmode_wake_from_sleep()` classifies a
network wake by the same handshake-completion rule a pin wake already
used. `REQ-PWRMODE-005`'s own text corrected in the same change (it had
baked in the wrong behavior as a formal requirement). Found and fixed a
second call site: `powerstate.c`'s legacy `rcp_powerstate_manager_
wake_via_network()` relied on the old skip; now synthesizes and
immediately completes a handshake locally to preserve its own
documented "always hot for network" contract without touching its
public signature. Mutation-tested two ways: full revert reproduces
exactly the 3 targeted pinned failures; a precise single-line mutation
isolated to `powerstate.c` alone confirms that fix's own independent
coverage. Full suite (64/64) + ASan/UBSan clean, fresh `cfusa
check`/`trace` (0 errors, 100%/100%, all three separate CI-matching
invocations). See `ROADMAP.md` milestone 164 for full detail. 1030
requirements (unchanged), 129 `tc18-gap` entries remaining (was 130,
one genuine closure).

### v0.163.0 -- 2026-08-10

**Phase 5c batch 1: REQ-PWRMODE-019 -- wake-handshake completion
actually re-enables endpoints.** Issue #199. First batch of the new
phase (15 requirements, 4 groups) -- started with the highest-severity
item: a woken server previously reported a completed hot start while
every endpoint stayed disabled. `rcp_pwrmode_handshake_resume_queues()`
(`power.h`) deliberately advances only a state enum, by design -- that
module never touches `server.h` (matching `lifecycle.h`/`discovery.h`'s
established "pure primitive, caller composes" layering) -- but no
caller anywhere in this codebase actually performed that composition.
Fixed: `mock.h` gains `rcp_mock_server_pwrmode_resume(srv, hs)`, which
calls the handshake primitive first and then re-enables every
registered endpoint on success. Kept `partial`: response-queue objects
and heartbeat-stream re-emission have no implementation anywhere in
this codebase yet (separate, already-tracked gaps). Also surfaced (not
acted on) a possible TC18 §12.4.1 internal terminology inconsistency
between "wake from sleep = cold start" and the detailed hot-start
procedure that follows it -- flagged for future scoping, not this
batch. Mutation-tested two ways: full revert of the mock layer alone
breaks the build; a precise single-line mutation isolates the one test
pinning real endpoint re-enable. Full suite (64/64) + ASan/UBSan clean,
fresh `cfusa check`/`trace` (0 errors, 100%/100%, all three separate
CI-matching invocations). See `ROADMAP.md` milestone 163 for full
detail. 1030 requirements (unchanged), 130 `tc18-gap` entries
unchanged in count (text re-scoped more precisely, not a full closure).

### v0.162.0 -- 2026-08-10

**Phase 5b batch 10: REQ-LIFECYCLE-033/029 -- REQUEST_REJECTED for
non-STANDARD EP0 requests.** Issue #198. `RCP_ERROR_REQUEST_REJECTED`
(11) existed but was emitted nowhere. `rcp_lifecycle_should_accept()`'s
`bool` return widened to a three-way `rcp_lifecycle_accept_t`
(`ACCEPT`/`DROP`/`REJECT`) -- no new parameter needed, since ABB
(STANDARD) vs. GBB (every conditional request kind) at the wire level
already fully determines the distinction via the existing `acf_msg_type`
parameter, contrary to the catalog entry's own assumption that a
`RCP_SCHED_KIND_*` input was required. A non-ABB message addressed to
EP0 now REJECTs (with a real `RCP_ERROR_REQUEST_REJECTED` response, via
`rcp_mock_server_dispatch()`) in both `HW_UNCONFIGURED` and
`HW_CONFIGURED`, instead of silently dropping. This also resolves
`REQ-LIFECYCLE-029`'s own residual, reconciling what looked like a
direct conflict between TC18 §12.3.1.2's general ACF_GBB-drop rule and
§12.7's more specific EP0-scoped REJECT rule -- the latter governs for
the EP0 case, the former still governs every non-EP0 `byte_bus_id`.
Every existing `should_accept()` call site rewritten from implicit-bool
assertions to explicit `RCP_LIFECYCLE_ACCEPT`/`DROP`/`REJECT` comparisons
(the old `bool` truthiness would otherwise silently invert, since
`ACCEPT == 0`). Mutation-tested two ways: full revert breaks the BUILD
(stronger signal than a test failure); a precise single-line mutation
isolates exactly the 4 tests pinning `REJECT`. Full suite (64/64) +
ASan/UBSan clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%, all
three separate CI-matching invocations). See `ROADMAP.md` milestone 162
for full detail. 1030 requirements (unchanged), 130 `tc18-gap` entries
remaining (was 132, two genuine closures). Phase 5b's full 16-item
scope is now accounted for (12 closed, 4 honestly-scoped `partial`).

### v0.161.0 -- 2026-08-10

**Phase 5b batch 9: REQ-LIFECYCLE-026/035/037 -- discovery-claim binding
to the HW_UNCONFIGURED and RCP_CONFIGURED gates.** Issue #198.
`REQ-LIFECYCLE-026`/`-035` were literal duplicates of one gap
(`RCP_LIFECYCLE_FIELD_HW_GENERIC` writable by any writer while
HW_UNCONFIGURED, no authorization check at all) -- both entries had
also named the wrong enforcement point (`rcp_lifecycle_should_accept()`,
a frame-admission filter) instead of the real one
(`rcp_lifecycle_field_writable()`, this codebase's established
write-authorization layer). Fixed: HW_GENERIC now requires
`writer.via_discovery_stream` while HW_UNCONFIGURED. `REQ-LIFECYCLE-037`
similarly named the wrong mechanism (`rcp_discovery_claim_note_
config_write()`/`_release()`, pure bookkeeping primitives that
correctly never consult lifecycle state) -- the real, confirmed gap was
`rcp_lifecycle_transition()`'s RCP_CONFIGURED -> HW_UNCONFIGURED reset
sharing one authorization check with the HW_CONFIGURED -> HW_UNCONFIGURED
reset, letting a bare discovery-stream writer demote a fully
RCP_CONFIGURED server -- forbidden by TC18 §12.7.4's "Changes in
configuration via a discovery request are no longer allowed." Fixed:
that specific reset now requires `writer.via_root_client_ep0`
specifically. `rcp_lifecycle_field_writable()`'s own RCP_CONFIGURED
field-write gate was already correct before this fix. Mutation-tested
two ways (full revert -> exactly 3 pinned tests fail; a precise
single-line mutation isolates the 2 tests pinning `-037` specifically).
Full suite (64/64) + ASan/UBSan clean, fresh `cfusa check`/`trace` (0
errors, 100%/100%, all three separate CI-matching invocations). See
`ROADMAP.md` milestone 161 for full detail. 1030 requirements
(unchanged), 132 `tc18-gap` entries remaining (was 135, three genuine
closures).

### v0.160.0 -- 2026-08-10

**Phase 5b batch 8: REQ-LIFECYCLE-023 + LOCKED_MEM_ACCESS/UNAUTHORIZED_ACCESS
correction to batch 6.** Issue #198. Scoping `REQ-LIFECYCLE-023`
required re-reading Figure 16's HW_CONFIGURED box, which carries a
transition batch 6 (v0.158.0) had not checked: writes to `HW_CONFIG`/
`QUEUE_CFG`/`EP_GEN_CFG` map to a diagram-only "LOCKED_CONFIG_ACCESS"
name -- unambiguously `RCP_ERROR_LOCKED_MEM_ACCESS` (4), contradicting
batch 6's conclusion (based on §13.7.1.2's prose alone) that every
denial maps to `UNAUTHORIZED_ACCESS` uniformly. `rcp_lifecycle_field_
write_error()` restored to its original two-tier design: state-driven
denials now correctly return `LOCKED_MEM_ACCESS`, writer/frame-driven
denials on top of an otherwise-permitting state still return
`UNAUTHORIZED_ACCESS` (batch 6's conclusion for that case was correct,
just not universal). `REQ-LIFECYCLE-023` itself closed with no new
code: `RCP_LIFECYCLE_FIELD_HW_GENERIC`'s existing rule already matches
Figure 16's `HW_CONFIG`/`QUEUE_CFG`/`EP_GEN_CFG` grouping exactly --
only its own doc comment needed to say so explicitly. `.fusa-reqs.json`
entries corrected a second time to record both the original mistake and
this correction transparently. Mutation-tested (a precise single-line
mutation fails exactly the 3 rewritten pinned tests across three
files). Full suite (64/64) + ASan/UBSan clean, fresh `cfusa check`/
`trace` (0 errors, 100%/100%). See `ROADMAP.md` milestone 160 for full
detail. 1030 requirements (unchanged), 135 `tc18-gap` entries remaining
(was 136, one genuine closure).

### v0.159.0 -- 2026-08-10

**Phase 5b batch 7: REQ-LIFECYCLE-022 EPs_NOT_IDLE gate, Figure 16
primary-source corrections.** Issue #198. Read TC18's Figure 16 as an
actual PDF page image (the extracted text renders this diagram too
poorly to trust) and found the `EPs_NOT_IDLE` gate applies to exactly
the `HW_UNCONFIGURED <-> HW_CONFIGURED` transitions, not
`HW_CONFIGURED -> RCP_CONFIGURED`. `rcp_lifecycle_transition()` gains
an `all_other_eps_idle` parameter and a new `RCP_LIFECYCLE_ERR_EPS_NOT_IDLE`
code, gating exactly those two transitions -- scoped `partial` since
Figure 16's own diagram-only "EPs_NOT_IDLE" name maps to none of the
seventeen numbered wire error codes anywhere in the spec, a genuine
TC18 inconsistency. Same image inspection also revealed
`RCP_CONFIGURED`'s own box has no equivalent "unknown stream/bb_id ->
ignore" transition at all, unlike `HW_UNCONFIGURED`'s/`HW_CONFIGURED`'s
-- corrected `REQ-LIFECYCLE-025`/`034`'s own catalog text to this
finding (genuine spec silence, not an implementable gap) rather than
leave them stale. Blast radius: 20 call sites via the same
required-parameter/compiler-enumeration technique as batch 5, zero
surprises. Mutation-tested two ways. Full suite (64/64) + ASan/UBSan
clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%, all three
gates verified as CI's own separate invocations). See `ROADMAP.md`
milestone 159 for full detail. 1030 requirements (unchanged), 136
`tc18-gap` entries remaining (unchanged count -- three status upgrades
within the gap category).

### v0.158.0 -- 2026-08-10

**Phase 5b batch 6: REQ-LIFECYCLE-024 write-denial error response,
wire-error-code correction.** Issue #198. New
`rcp_lifecycle_field_write_error()` (`REQ-WIREERR-004`) maps
`rcp_lifecycle_field_writable()`'s bool to `RCP_ERROR_NONE` or
`RCP_ERROR_UNAUTHORIZED_ACCESS`. **Primary-source verification caught a
real error in this catalog entry's own prior text**: it named
`RCP_ERROR_LOCKED_MEM_ACCESS` as the expected wire code, but TC18
§13.7.1.2's own worked example -- the only concrete guidance the spec
gives for either code -- assigns a write-prohibited-register denial
`UNAUTHORIZED_ACCESS` uniformly, regardless of whether the denial is
state-driven or writer-driven. `RCP_ERROR_LOCKED_MEM_ACCESS` corresponds
to the separate, still-unmodeled `svr_configuration_lock` mechanism and
is deliberately never emitted by the new function. An existing
deviation-pin test that repeated the same mistaken assumption rewritten
to match. Mutation-tested two ways (new-API build break; a precise
single-line mutation of the mapping itself). Full suite (64/64) +
ASan/UBSan clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%). See
`ROADMAP.md` milestone 158 for full detail. 1030 requirements (was
1029, `REQ-WIREERR-004` added), 136 `tc18-gap` entries remaining (was
137).

### v0.157.0 -- 2026-08-10

**Phase 5b batch 5: REQ-LIFECYCLE-031 svr_lifecycle_state write
authorization.** Issue #198. `rcp_lifecycle_transition()` gains a
`rcp_lifecycle_writer_ctx_t writer` parameter and a new
`RCP_LIFECYCLE_ERR_UNAUTHORIZED` error code -- TC18 §12.3.1.2 requires a
`svr_lifecycle_state` write be accepted only via the discovery stream
or the root client; previously any caller could promote or demote the
server's lifecycle state unconditionally. Scoped `partial`, not
`implemented`: TC18's further narrowing ("any valid stream when no root
client is configured") can't yet be expressed given this library's
current architecture (same gap as `REQ-LIFECYCLE-025`/`034`) --
conservatively requires the root client in both cases rather than
accept an unqualified stream. Blast radius small and fully enumerable
this batch (a required parameter, unlike batches 3/4's additive struct
field, forces every call site into view via compiler errors -- no
shared-default surprise possible). Mutation-tested two ways (build
break for the signature change, a precise single-line logic mutation
for the authorization check itself). Full suite (64/64) + ASan/UBSan
clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%). See
`ROADMAP.md` milestone 157 for full detail. 1029 requirements
(unchanged), 137 `tc18-gap` entries remaining (unchanged count --
status upgrade within the gap category).

### v0.156.0 -- 2026-08-10

**Phase 5b batch 4: REQ-LIFECYCLE-030 + REQ-LIFECYCLE-036 HW_CONFIGURED
write authorization.** Issue #198. Both requirements reduce to the same
condition: TC18 §12.3.1.2/§12.7.3 require HW_CONFIGURED functional-
config write access be gated on the root client via EP0, the
endpoint's own owning stream, or the discovery stream --
`rcp_lifecycle_field_writable()`'s `HW_CONFIGURED` branches previously
granted access unconditionally. New `via_discovery_stream` member on
`rcp_lifecycle_writer_ctx_t`; `HW_CONFIGURED` now gates on `authorized
|| writer.via_discovery_stream`. Blast-radius checked proactively
before writing code (this batch's own explicit lesson from batch 3) --
found and migrated ~35 affected tests across 14 files (a shared
`any_writer()` helper fix cleared ~20 at once; 11 DEVIATION-PIN-style
tests rewritten to assert the corrected behavior; 3
`test_tc18_gaps_regmap.c` sites individually reworked to preserve their
real point without depending on the now-authorization-gated default).
Mutation-tested (reverting `src/lifecycle.c` alone reproduces the exact
same 13-binary failure set the pre-fix build showed, confirming every
rewritten test actually pins the new behavior). Full suite (64/64) +
ASan/UBSan clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%).
See `ROADMAP.md` milestone 156 for full detail. 1029 requirements
(unchanged), 137 `tc18-gap` entries remaining (was 139).

### v0.155.0 -- 2026-08-10

**Phase 5b batch 3: REQ-LIFECYCLE-027 write requests unicast-only.**
Issue #198, the batch's own flagged highest-severity item ("a single
broadcast/multicast write frame can reconfigure every RC Server on the
network at once"). `rcp_lifecycle_writer_ctx_t` gains
`via_non_unicast_frame`; `rcp_lifecycle_field_writable()` now denies an
otherwise-writable field (any of `HW_GENERIC`/`FUNCTIONAL_W`/
`FUNCTIONAL_W_STAR`, any state) whenever it is set, per TC18
§12.3.1.1/§12.3.1.2/§12.3.1.3's identical restated rule.
`rcp_regmap_writer_ctx()` gains a `via_unicast` parameter, the one
production derivation path this library has, closing the loop. New
`rcp_l2_mac_is_unicast()` primitive (`REQ-L2-011`) classifies a real
destination MAC via the IEEE 802.3 individual/group bit for callers
that need one. `rcp_lifecycle_should_accept()` deliberately untouched
-- TC18's rule is scoped to write requests, not general frame
admission, so the gate belongs at `field_writable()` alone. Blast-radius
checked before scoping (this batch's own explicit lesson from batch 2):
adding a *struct field* rather than a bare parameter means ~150
existing writer_ctx literals across the test suite needed zero changes,
C's partial-brace-init rule zero-initializing the new member to
"unicast/compliant" by default; only `rcp_regmap_writer_ctx()`'s 8 call
sites needed updating. Mutation-tested three ways (new-API build break
for `l2.c`, runtime assertion failure for `lifecycle.c`'s behavior
change, and a precise single-line assignment-inversion mutation for
`regmap.c` isolating the plumbing itself from its signature). Full
suite (64/64) + ASan/UBSan clean, fresh `cfusa check`/`trace` (0
errors, 100%/100%). See `ROADMAP.md` milestone 155 for full detail.
1029 requirements (was 1028, `REQ-L2-011` added), 139 `tc18-gap`
entries remaining (was 140).

### v0.154.0 -- 2026-08-10

**Phase 5b batch 2: REQ-LIFECYCLE-032 HW_CONFIGURED admits only EP0.**
Issue #198. `rcp_lifecycle_should_accept()` now restricts
`HW_CONFIGURED` to `RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID` (EP0), per TC18
§12.3.1.2's "requests to EPs other than EP0 that are not config
requests will be ignored and dropped". c-RCP has no wire-level
encode/decode pair for a functional-configuration request at all yet
(confirmed by grepping the whole codebase), so every non-EP0 request
is, by construction, operational -- making the EP0-only restriction the
honestly-achievable form of this rule. Attempted once already in this
same batch and reverted mid-way when it broke ~35 tests across
`test_mock.c` and `test_conditional_dispatch.c` that implicitly assume
ordinary dispatch works in `HW_CONFIGURED`; redone as a two-step plan
(migrate affected fixtures to `RCP_CONFIGURED` first, verified
independently, then reapply the fix as a clean diff). Mutation-tested,
full suite (64/64) + ASan/UBSan clean, fresh `cfusa check`/`trace` (0
errors, 100%/100%). See `ROADMAP.md` milestone 154 for full detail.
1028 requirements (unchanged count), 140 `tc18-gap` entries remaining
(was 141).

### v0.153.0 -- 2026-08-10

**Phase 5b batch 1: REQ-LIFECYCLE-028 HW_CONFIGURED drops TSCF
unconditionally.** Issue #198. `rcp_lifecycle_should_accept()` now
drops a TSCF-headed AVTPDU in `HW_CONFIGURED` regardless of
time-sync support (TC18 §12.3.1.2), the same unconditional rule
already applied to `HW_UNCONFIGURED`. Primary-source verification
caught a real typo in the TC18 spec PDF itself (§12.3.1.2's heading
wrongly repeats "HW_UNCONFIGURED"; its content is unambiguously about
HW_CONFIGURED) before trusting the citation. The sibling
REQ-LIFECYCLE-029 (ACF_GBB drop, same section) was deliberately NOT
implemented alongside this: every conditional request kind is
wire-encoded as ACF_GBB unconditionally, so dropping it in
HW_CONFIGURED would make conditional requests unsubmittable there at
all -- entangled with the not-yet-implemented REQ-LIFECYCLE-032
instead. Also updates a pre-existing (pre-gap-audit) `test_lifecycle.c`
test that asserted the now-superseded permissive behavior. Mutation-
tested, full suite (64/64) + ASan/UBSan clean, fresh `cfusa
check`/`trace` (0 errors, 100%/100%). See `ROADMAP.md` milestone 153
for full detail. 1028 requirements (unchanged count), 141 `tc18-gap`
entries remaining (was 142).

### v0.152.0 -- 2026-08-09

**Phase 5a batch 7 (final): REQ-E2E-038 fragmented-message CRC
coverage primitive.** Issue #197. Adds
`rcp_e2e_compute_fragmented_crc()`, computing TC18 §13.6's fragmented-
message CRC span (stream_id + avtp_timestamp + the FIRST fragment's ACF
header + the concatenated payload of EVERY segment, not just the final
fragment's own bytes as `rcp_e2e_wrap()`-based dispatch does today) --
the same running-CRC-over-several-regions technique
`rcp_e2e_compute_crc()` already uses. Honestly scoped `partial`, not
`implemented`: nothing in this codebase calls the new primitive yet,
and `mock.c` has no fragmented-message dispatch path at all (protected
or not) to wire it into -- a materially larger, separate architecture
item. Mutation-tested (the test file fails to build without the fix),
full suite + ASan/UBSan clean, fresh `cfusa check`/`trace` (0 errors,
100%/100%). See `ROADMAP.md` milestone 152 for full detail, including
the closing summary for Phase 5a (issue #197) as a whole. 1028
requirements (unchanged count), 142 `tc18-gap` entries remaining
(unchanged count -- stays a gap entry, now `partial`).

### v0.151.0 -- 2026-08-09

**Phase 5a batch 6: REQ-E2E-037 AVTPDU data-length adjustment helper.**
Issue #197. `rcp_avtp_encode_ntscf()`/`_encode_tscf()` already computed
the +4-octets-per-protected-member length adjustment TC18 §13.6
requires correctly and automatically (proven by the existing test's
bogus-header-value check) -- what was missing was purely that no
function gave the rule its own name. Adds
`rcp_e2e_data_length_for_protected_members()`, a pure arithmetic
helper alongside `rcp_e2e_length_with_crc()`. Mutation-tested (the
test file fails to build without the fix), full suite + ASan/UBSan
clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%). See
`ROADMAP.md` milestone 151 for full detail. 1028 requirements
(unchanged count), 142 `tc18-gap` entries remaining (was 143).

### v0.150.0 -- 2026-08-09

**Phase 5a batch 5: REQ-E2E-031/033/041 CRC verification wired into
mock.c's dispatch path.** Issue #197. Adds `rcp_mock_server_dispatch_e2e()`/
`_dispatch_frame_e2e()`, new additive E2E-aware counterparts to
`rcp_mock_server_dispatch()`/`_dispatch_frame()` (existing signatures
untouched) that consult a new per-endpoint `req_crc_enable` flag
(`rcp_mock_server_set_endpoint_req_crc_enable()`) and, when set, verify
each request's CRC32 via `rcp_e2e_unwrap_framed()` before admission: a
mismatch is never even admitted and produces a real Table 27
`POCI_FAILURE` error response (`rcp_acf_build_error_response()`); a
match dispatches the unwrapped payload normally. The frame variant
verifies each member of a multi-ACF frame independently. This closes
the "biggest architecture decision in the phase" flagged since batch 1:
the two pure primitives added in batches 3-4 were correct but
unreachable from any real call path until now. `.fusa-reqs.json`'s
`REQ-E2E-031`/`REQ-E2E-033`/`REQ-E2E-041` move `tc18-gap`/`partial` ->
`tc18` (fully implemented). Mutation-tested (the test file fails to
build without the fix, 12 errors), full suite + ASan/UBSan clean
(first batch this session adding new heap alloc/free paths), fresh
`cfusa check`/`trace` (0 errors, 100%/100%). See `ROADMAP.md` milestone
150 for full detail. 1028 requirements (unchanged count), 143
`tc18-gap` entries remaining (was 146 -- the first net decrease since
Phase 5a began).

### v0.149.0 -- 2026-08-09

**Phase 5a batch 4: REQ-E2E-028/029 sequence-number enforcement
primitive.** Issue #197. Adds `rx_enforce_seq`/`rx_seq_safestate_enable`
to `rcp_regmap_request_stream_cfg_t` (TC18 §12.7.7 Table 22) and a new
pure primitive, `rcp_e2e_seq_evaluate()` with caller-owned
`rcp_e2e_seq_tracker_t` state, deciding whether an AVTPDU's
`sequence_num` should be accepted and whether it constitutes a
safety-relevant discontinuity -- using RFC 1982 serial-number
comparison so the 8-bit counter's wraparound is handled correctly. The
tracker advances only on accept, so a rejected replay can't drag the
reference point backward. Not yet wired into any admission path
(`rcp_server_endpoint_admit()` has no `sequence_num` input; that
integration is a separate, larger change) -- documented honestly as
`partial`, not `implemented`. Mutation-tested (the test file fails to
build without the fix), full suite + ASan/UBSan clean, fresh `cfusa
check`/`trace` (0 errors, 100%/100%). See `ROADMAP.md` milestone 149
for full detail. 1028 requirements (unchanged count), 146 `tc18-gap`
entries remaining (unchanged count -- both stay gap entries, now
`partial`).

### v0.148.0 -- 2026-08-09

**Phase 5a batch 3: REQ-E2E-030 request-storage-overflow error code.**
Issue #197. `rcp_server_endpoint_admit()` now sets
`*out_error = RCP_ERROR_REQUEST_STORAGE_OVERFLOW` when an endpoint's
request storage is full (TC18 §12.7.7 Table 22
`rx_ovrflw_safestate_enable`), so a caller can build a real Table 27
error response instead of the request being dropped with no
diagnostic (`mock.c`'s `finish_admission()` already does this
generically -- no `mock.c` change needed). Also adds
`rcp_e2e_overflow_should_enter_safe_state()`, the pure decision
primitive a future cross-endpoint orchestrator would consult to
perform the stream-wide safe-state escalation TC18 also requires --
which this single-endpoint call cannot itself perform, an honest
architecture boundary documented in code and in `.fusa-reqs.json`
(status moves `not-implemented` → `partial`, stays a tracked
`tc18-gap`, not claimed `implemented`). Mutation-tested (the test file
fails to build without the fix), full test suite + ASan/UBSan clean,
fresh `cfusa check`/`trace` (0 errors, 100%/100%). See `ROADMAP.md`
milestone 148 for full detail. 1028 requirements (unchanged count), 146
`tc18-gap` entries remaining (unchanged count -- this one stays a gap
entry, now `partial`).

### v0.147.0 -- 2026-08-09

**Phase 5a batch 2: REQ-E2E-035 NTSCF-framed wrappers.** Issue #197.
Added `rcp_e2e_wrap_framed()`/`rcp_e2e_unwrap_framed()`: framing-aware
convenience wrappers that force the CRC's `avtp_timestamp` contribution
to zero for NTSCF-framed traffic (TC18 §13.6), regardless of what
timestamp the caller passed in. The raw `rcp_e2e_wrap()`/`_unwrap()`
primitives are unchanged and stay general-purpose. Mutation-tested (the
test file fails to build without the fix), full test suite + ASan/UBSan
clean, fresh `cfusa check`/`trace` (0 errors, 100%/100%). See
`ROADMAP.md` milestone 147 for full detail. 1028 requirements (unchanged
count), 146 `tc18-gap` entries remaining (was 147).

### v0.146.0 -- 2026-08-09

**Phase 5a batch 1: REQ-E2E-042 quadlet-alignment enforcement.** Issue
#197. First real behavioral gap-closure fix (not a citation) since Phase
2 concluded. `rcp_e2e_wrap()` now rejects a non-quadlet-aligned
`acf_frame_len` instead of silently appending a misaligned CRC32 trailer
-- TC18 §13.6 Figures 19/20 require the trailer to occupy the message's
final whole quadlet. Mutation-tested, full test suite + ASan/UBSan clean,
fresh `cfusa check`/`trace` (0 errors, 100%/100%). Also fixed three
quadlet-misaligned synthetic test fixtures in `tests/test_e2e.c` the new
enforcement correctly caught. See `ROADMAP.md` milestone 146 for full
detail. 1028 requirements (unchanged count), 147 `tc18-gap` entries
remaining (was 148).

### v0.145.0 -- 2026-08-07

**Citation backfill, batch 25: TIMED.** Issue #164. Cited 6 of 7
remaining uncited timed-request requirements against TC18 §11.2.2.5's
Figure 12/Table 10 wire format and its PRESENTATION_TIME_TOO_FAR/
GPTP_FAIL rejection rules. This closes out the full conditional-
request family opened by CMP (batch 16): CMP, SCHED, TRIG, CHAIN,
CANCEL, SEQ, and TIMED are all now cited. See `ROADMAP.md` milestone
145 for full detail. 1028 requirements (unchanged), 100% traced+
tested, 0 `cfusa check` errors.

### v0.144.0 -- 2026-08-07

**Citation backfill, batch 24: SEQ.** Issue #164. Cited all 10
remaining uncited sequencer-primitive requirements against TC18
§12.10 (Sequencers) and §12.7.10 (Sequencer state registers, Table
25's SEQUENCER_config register map) -- SEQ is now 100% cited. This is
the shared state-table primitive CMP/SRV both consume, previously
cited only indirectly through those callers. See `ROADMAP.md`
milestone 144 for full detail. 1028 requirements (unchanged), 100%
traced+tested, 0 `cfusa check` errors.

### v0.143.0 -- 2026-08-07

**Citation backfill, batch 23: CANCEL.** Issue #164. Cited 7 of 8
remaining uncited cancellation-request requirements against TC18
§11.2.3's three cancellation mechanisms (clear-all, clear-single, plus
the cancellable-window, outcome, and chain-cascade predicates). A
second id-mapping error (`REQ-CANCEL-007`, already cited) caught and
fixed by the pre-flight check before any edit ran. See `ROADMAP.md`
milestone 143 for full detail. 1028 requirements (unchanged), 100%
traced+tested, 0 `cfusa check` errors.

### v0.142.0 -- 2026-08-07

**Citation backfill, batch 22: CHAIN.** Issue #164. Cited 8 of 9
remaining uncited chained-request requirements against TC18
§11.2.2.4's Figure 11 wire format, the no-predecessor CHAIN_ERROR
rule, the cs-bit-driven CHAIN_ABORTED rule, and the reserved-octet
rejection rule. See `ROADMAP.md` milestone 142 for full detail. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.141.0 -- 2026-08-07

**Citation backfill, batch 21: TRIG.** Issue #164. Cited 8 of 9
remaining uncited triggered-request requirements against TC18
§11.2.2.3's Figure 10/Table 8 wire format and §12.9.3 Table 26's
Triggered-request execution-procedure row. See `ROADMAP.md` milestone
141 for full detail. 1028 requirements (unchanged), 100% traced+
tested, 0 `cfusa check` errors.

### v0.140.0 -- 2026-08-07

**Citation backfill, batch 20: SCHED.** Issue #164. Cited all 8
uncited `scheduler.c` requirements -- SCHED is now 100% cited.
Request-kind priority ranking/comparison against §12.9.2's seven-tier
priority list (already cited for SRV, whose own due-selection function
calls this module's `rcp_sched_compare()`); multi-ACF-per-frame
splitting and TSCF timing consistency against §12.9.1.1. See
`ROADMAP.md` milestone 140 for full detail. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.139.0 -- 2026-08-07

**Citation backfill, batch 19: SRV.** Issue #164. Cited all 15
remaining uncited requirements for `server.c`'s request-storage/
admission/priority-scheduling/completion core against TC18 §12.9.1's
request handling, §12.9.2's seven-tier priority-in-execution ordering,
and §12.9.3 Table 26's per-request-type execution procedure -- SRV is
now 100% cited. Also records that PWR (`powerstate.c`) was scoped and
rejected as a batch candidate: a thin client-side wrapper with no
distinct TC18 text of its own. See `ROADMAP.md` milestone 139 for full
detail. 1028 requirements (unchanged), 100% traced+tested, 0
`cfusa check` errors.

### v0.138.0 -- 2026-08-07

**Citation backfill, batch 18: WAKEUP.** Issue #164. Cited 13 of 16
remaining uncited Wakeup-control requirements against TC18 §13.7.2's
basic-concept prose, Table 36/37's register block, and §13.7.2.3's
sleep-request handshake (Figure 22, SleepCMD=0xA5). Several citations
cross-reference the module's pre-existing `tc18-gap` entries
(`REQ-WAKEUP-017`-`022`) rather than implying full conformance where
the implementation is a documented, reduced model of the spec's fuller
register/behavior encoding. Also records that MDIO was scoped and
rejected as a batch candidate this session -- its wire layout is
almost entirely self-documented as non-TC18 original design. See
`ROADMAP.md` milestone 138 for full detail. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.137.0 -- 2026-08-07

**Citation backfill, batch 17: LINEP.** Issue #164. Cited 12 of 16
remaining uncited LIN-commander requirements against TC18 §13.7.10's
basic-concept prose (single transmission-done trigger, the
checks-each-received-message reply rule), Table 52 functional config
(`lin_clk_divider`), and Figure 38's request/response format. Left the
trigger-select setter uncited alongside the standing zero-init/
`strerror()` pattern -- same no-register-basis situation as
`REQ-ISELED-014`. See `ROADMAP.md` milestone 137 for full detail. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.136.0 -- 2026-08-07

**Citation backfill, batch 16: CMP.** Issue #164. Cited 16 of 16
remaining uncited compound/compound-wait conditional-request
requirements against TC18 §11.2.2's rich Table 5/6/7 + Figure 8/9
basis (request_type dispatch, encode-time validation, and the
sequencer advance-only-if-still-in-start_state behavioral core).
Unlike ISELED (v0.134.0), this module's own "original design"
disclaimer covers only its exact byte packing, not the underlying
behavior -- which traces directly to well-defined TC18 prose. See
`ROADMAP.md` milestone 136 for full detail. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.135.0 -- 2026-08-07

**Citation backfill, batch 15: I2C.** Issue #164. Cited 15 of 17
remaining uncited I2C-endpoint requirements against TC18 §13.7.7's
basic-concept prose (ACF_ABB/ACF_GBB acceptance, genuinely
bidirectional read/write op sense), Table 46 functional config
(`i2c_mode`, cross-referencing the pre-existing `REQ-I2C-019` gap for
Table 46's own duplicated high-speed-mode labeling rather than
re-diagnosing it), and Figure 29's i2c request format plus the
7-/10-bit-address-transparent passthrough text. See `ROADMAP.md`
milestone 135 for full detail. 1028 requirements (unchanged), 100%
traced+tested, 0 `cfusa check` errors.

### v0.134.0 -- 2026-08-07

**Citation backfill, batch 14: ISELED.** Issue #164. Cited 9 of 23
remaining uncited ISELED-endpoint requirements against TC18 §13.7.12's
Freq_Sync/ISP_N clock-recovery prose, single-trigger description,
Table 55 functional-config registers, CRC-enable prose, and Figure
40/41 request/response formats. Left the bulk of the module (symbol
encode/decode, bitframe framing, CRC-8 algorithm, decode error paths,
trigger-select setter) uncited: the module's own file header states
these are this implementation's original design, not TC18-specified
content -- TC18 names the ISLED bit-encoding by reference only, without
giving its bit-level scheme. Filed issue #184 to track two open TC18
ambiguities (`REQ-ACF-012`, `PWM_IN_NO_SIGNAL` sentinel) surfaced
earlier in this backfill. See `ROADMAP.md` milestone 134 for full
detail. 1028 requirements (unchanged), 100% traced+tested, 0
`cfusa check` errors.

### v0.133.0 -- 2026-08-07

**Citation backfill, batch 13: CANEP.** Issue #164. Cited 19 of 21
remaining uncited CAN-endpoint requirements against TC18 §13.7.11's
frame-format (Table 54), functional-config (Table 53), request/
response (Figure 39), and fragmentation subsections -- largely reusing
citation text already established by earlier-cited sibling
requirements in the same module. Used the pre-flight citation-target
check added after batch 11. Left 2 uncited (functional-config
zero-init, `strerror()` uniqueness) -- genuine implementation detail.
Purely additive; no code or test changed. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.132.0 -- 2026-08-07

**Citation backfill, batch 12: E2E.** Issue #164. First batch to use
the pre-flight target-verification check added after batch 11. Cited
26 of 28 remaining uncited E2E requirements, spanning two distinct
TC18 regions: the CRC32 mechanism itself (§13.6 Table 31 and the ABB/
GBB CRC coverage rules) and the safety/watchdog machinery, which lives
separately in §11.2.2's safety-request MSB convention and §12.7.7
Table 22's per-stream register block. Left 2 uncited (`strerror()`
uniqueness, defensive fail-safe on invalid input) -- genuine
implementation detail. Purely additive; no code or test changed. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.131.0 -- 2026-08-07

**Citation backfill, batch 11: ADC.** Issue #164. Cited 22 of 28
remaining uncited ADC requirements against TC18 §13.7.9's averaging,
functional-config (Table 51), trigger (Table 50), and request-handling
subsections, plus the general Table 30 row. Left 6 uncited: 4 for the
`RCP_EP_PWM_IN_NO_SIGNAL` sentinel ambiguity already flagged in batch
6, 1 zero-init, 1 `strerror()` uniqueness. This batch's own citation
script had a self-caught bug (a target already cited going in shifted
four subsequent edits by one requirement) -- fixed before commit; all
5 prior script-based batches were audited against their base-commit
state and confirmed unaffected. Purely additive; no code or test
changed. 1028 requirements (unchanged), 100% traced+tested, 0
`cfusa check` errors.

### v0.130.0 -- 2026-08-07

**Citation backfill, batch 10: DISC.** Issue #164. First non-endpoint-
type batch: DISC is the RC-Server-level discovery protocol (§12.6).
Cited 27 of 28 remaining uncited DISC requirements against §12.6.1
Table 16 (discovery request format), §12.6.2 Table 17 (discovery
response format), and §12.6's own prose for the discovery-stream claim/
timeout/preemption mechanism, which has no dedicated table. Left 1
uncited (`strerror()` uniqueness) -- genuine implementation detail.
Purely additive; no code or test changed. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.129.0 -- 2026-08-07

**Citation backfill, batch 9: UART.** Issue #164. Cited 27 of 29
remaining uncited UART requirements. UART's evt[2:0] carries no
per-value meaning of its own in TC18 §13.5 Table 30 -- grouped with
ADC/PWM_IN/I2C/LIN/CAN/ISELED/MDIO where only evt=111b (reconfig) is
defined. Cited bit-padding rules, Table 48's functional-config fields
paired with the established §12.3.1.2/§12.3.1.3 lifecycle-writability
basis, and §13.7.8.1's read-completion/fragmentation rules for wire
format. Two citations (`set_baud_rate`, `set_timeout`) explicitly
cross-reference the pre-existing unit-divergence gap at
`REQ-UART-037`; `set_rx_buffer_size` cites TC18's prose fifo
description rather than a nonexistent Table 48 register, consistent
with the existing gap notes at `REQ-UART-032`/`036`. Left 2 uncited
(functional-config zero-init, `strerror()` uniqueness) -- genuine
implementation detail. Purely additive; no code or test changed. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.128.0 -- 2026-08-07

**Citation backfill, batch 8: SPI.** Issue #164. Cited 28 of 30
remaining uncited SPI requirements against SPI's own distinct §13.5
Table 30 row (channel-select 000b-101b, reserved 110b, reconfig 111b --
read fresh, not reused from GPIO/PWM_OUT's row), §13.7.3.1's 6-channel
description and Table 38 trigger outputs, Table 39's per-channel
functional-config fields, and §13.7.3.3's read-direction transfer
semantics with Figure 23's worked example. Lifecycle-gated writability
reuses the §12.3.1.2/§12.3.1.3 basis from prior batches. Left 2 uncited
(`strerror()` uniqueness, functional-config zero-init) -- genuine
implementation detail. Purely additive; no code or test changed. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.127.0 -- 2026-08-07

**Citation backfill, batch 7: GPIO.** Issue #164. Cited 30 of 32
remaining uncited GPIO requirements. evt[2:0] write-modifier semantics
reuse the same §13.5 Table 30 GPIO/PWM_OUT row citations as the
previous batch's PWM_OUT half; lifecycle-gated writability reuses the
§12.3.1.2/§12.3.1.3 basis established for LIFECYCLE/PWM; request/
response wire format cites §13.7.4.1's GPIO-specific 4-byte/
INVALID_PARAMETER rule plus the general Table 30 frame-validation
basis; trigger semantics cite Table 40. Left 2 uncited (`strerror()`
uniqueness, functional-config zero-init) -- genuine implementation
detail. Purely additive; no code or test changed. 1028 requirements
(unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.126.0 -- 2026-08-07

**Citation backfill, batch 6: PWM.** Issue #164. First per-endpoint-type
batch (PWM was the single largest remaining uncited category, 54/58).
Cited 48 of 54 against §13.5 Table 30's GPIO/PWM_OUT evt[2:0]
write-modifier row, §13.7.5/§13.7.6's trigger tables and request/
response format, the §12.3.1.2/.3 lifecycle-writability basis already
established for LIFECYCLE, and §13.5.1's compound-wait comparison
clauses. Left 6 uncited: 4 implementation-detail (zero-init,
`strerror()` uniqueness) and 2 (`RCP_EP_PWM_IN_NO_SIGNAL` round-trip/
compound-wait exclusion) where the 0xFFFF sentinel has no direct TC18
textual basis -- flagged as a genuine ambiguity, not force-cited.
Purely additive; no code or test changed. 1028 requirements (unchanged),
100% traced+tested, 0 `cfusa check` errors.

### v0.125.0 -- 2026-08-07

**Citation backfill, batch 5: PWRMODE.** Issue #164. Cited 10 of 12
remaining uncited PWRMODE requirements against TC18 §12.4.1's
cold-start/hot-start-up procedure and §12.5's StandBy/Sleep entry
gating. Two new citations cross-reference pre-existing gap requirements
(`REQ-PWRMODE-014` cold-start-target divergence, `REQ-PWRMODE-020`
network-wake-handshake-bypass divergence) rather than citing over them
-- both findings predate this batch. Left 2 uncited (`string()`/
`strerror()` uniqueness) -- genuine implementation detail. Purely
additive; no code or test changed. 1028 requirements (unchanged), 100%
traced+tested, 0 `cfusa check` errors.

### v0.124.0 -- 2026-08-07

**Citation backfill, batch 4: LIFECYCLE.** Issue #164. Cited 17 of 19
remaining uncited LIFECYCLE requirements against TC18 §12.3.1's three
lifecycle-state subsections. Two pre-existing, already-tracked gap
requirements (`REQ-LIFECYCLE-022` EPs_NOT_IDLE demotion gate,
`REQ-LIFECYCLE-028` HW_CONFIGURED TSCF-drop divergence) are
cross-referenced from the new citations rather than duplicated -- both
findings predate this batch, this batch just connects the dots. Left 2
uncited (same-state no-op, `strerror()` uniqueness) -- genuine
implementation detail. Purely additive; no code or test changed. 1028
requirements (unchanged), 100% traced+tested, 0 `cfusa check` errors.

### v0.123.0 -- 2026-08-07

**Citation backfill, batch 3: RMAP.** Issue #164. Cited 15 of 22
remaining uncited RMAP requirements against four TC18 sources: §12.3.1.1
(EP0/discovery `byte_bus_id`), §13.3 Table 33 (`svr_root_client_index`),
§12.9.1.1 (compound-request options-group bundling), §12.3.1.2
(root-client and owning-stream write-access grants), and §13.2 Table 23
(EP_ID_config's required ascending ordering). Left 7 uncited (pin-property
bitmask distinctness, string-helper non-NULL/uniqueness guarantees,
config-struct zero-init boilerplate) -- genuine implementation-detail
requirements with no TC18 clause, not an oversight. Purely additive; no
code or test changed. 1028 requirements (unchanged), 100% traced+tested,
0 `cfusa check` errors.

### v0.122.0 -- 2026-08-07

**Citation backfill, batch 2: ACF.** Issue #164. Cited 11 of 14 remaining
uncited ACF requirements against TC18 §11.2.1 Figure 7/Table 4 (the
`byte_message_info` header ABB and GBB share). Left 3 uncited pending
closer investigation rather than a speculative citation, including one
genuine open question (`REQ-ACF-012`'s `RCP_ACF_MTV_UNCERTAIN` third
state -- TC18's own `mtv` bit is binary; may be conflating it with the
AVTP TSCF header's separate `tu` bit). Purely additive; no code or test
changed. 1028 requirements (unchanged), 100% traced+tested, 0 `cfusa
check` errors.

### v0.121.0 -- 2026-08-07

**Citation backfill, batch 1: AVTP.** Issue #164. Cited 11 of 20 AVTP
requirements against TC18 §11.1's NTSCF/TSCF header definitions and
worked wire-trace examples. The remaining 9 (stream_id/address equality,
transport plumbing) are genuine implementation-detail requirements with
no specific TC18 clause, deliberately left uncited. Purely additive; no
code or test changed. 1028 requirements (unchanged), 100% traced+tested,
0 `cfusa check` errors.

### v0.120.0 -- 2026-08-07

**Fifth real error response: `EP_NOT_FOUND` for an unregistered byte_bus_id.**
Issue #163 batch E (first half). `rcp_mock_server_dispatch()` now sends a
real error response when `byte_bus_id` names no registered endpoint --
the value is already known (the function's own parameter), and
`transaction_num` is read back out of the request's own header.
`rcp_mock_server_dispatch_frame()` gets this for free via delegation. The
Table 27 note's second `EP_NOT_FOUND` trigger (a Trigger request's
`trigger_source_ep` naming a nonexistent endpoint) is real new feature
work -- no endpoint-registry validation exists for it anywhere -- and is
tracked separately, not attempted here. 1028 requirements
(`REQ-MOCK-030` added), 100% traced+tested, 0 `cfusa check` errors.
Mutation-tested; full build + test suite + ASan/UBSan pass.

### v0.119.0 -- 2026-08-07

**Third and fourth real error responses: chained `CHAIN_ERROR`/`CHAIN_ABORTED`.**
Issue #163 batch D. A chained request with no predecessor sends
`CHAIN_ERROR` (TC18 §11.2.2.4); `cs=1` after a predecessor errored sends
`CHAIN_ABORTED`. `rcp_chained_advance()` already classified both
correctly; `mock.c` discarded the response in both branches. Each
affected frame member gets its own independent response (no
multi-response fanout problem here, unlike `REQUEST_CANCELED`'s still-open
case) -- the new test pins two different members getting two different
responses. Also fixed a pre-existing test's response leak, caught by this
milestone's own ASan run (harmless before, real after). 1027 requirements
(`REQ-MOCK-029` added), 100% traced+tested, 0 `cfusa check` errors.
Mutation-tested; full build + test suite + ASan/UBSan pass.

### v0.118.0 -- 2026-08-07

**Second real error response: clear-single's `REQUEST_NOT_FOUND`.**
Issue #163 batch A. `rcp_server_endpoint_cancel_single()` already reported
`RCP_CANCEL_RESULT_NOT_FOUND`; `mock.c` discarded it. Now builds and
returns a real TC18 §11.2.3.3 error response, carrying the cancellation
request's own `byte_bus_id`/`transaction_num` (§12.9.6's general rule),
not the not-found target's. `REQUEST_CANCELED` (every request a
cancellation *does* remove also gets its own error response, per §11.2.3)
needs a multi-response fanout the current API can't represent -- scoped
out, tracked separately. 1026 requirements (`REQ-MOCK-028` added), 100%
traced+tested, 0 `cfusa check` errors. Mutation-tested; full build + test
suite + ASan/UBSan pass.

### v0.117.0 -- 2026-08-07 (BREAKING)

**First real TC18 §12.9.6 error response.** An audit (issue #163) found
this library never actually constructed a wire-level Error Response
anywhere, including its own `mock.c` reference integration -- the 17-code
`rcp_wire_error_t` enum and `rcp_e2e_wire_error()`'s CRC mapping only ever
produced a *value*, never response bytes. Added `rcp_acf_build_error_response()`
(new primitive) and wired the one rejection path that already has
everything TC18 §12.9.6 requires decoded (a compound-wait request's
reserved `evt[2:0]=011b`, which `server.c`'s own pre-existing comment
already quoted the spec's requirement for). `rcp_server_endpoint_admit()`
gains a new `rcp_wire_error_t *out_error` output parameter -- **every
caller must update its call site** (two in this repo: `mock.c` and the
test suite). The other seven rejection paths and 15 of 16 still-unmapped
Table 27 codes remain open, tracked in issue #163. 1025 requirements
(`REQ-ACF-031`, `REQ-SRV-022` added), 100% traced+tested, 0 `cfusa check`
errors. Full build + test suite + ASan/UBSan pass; the new behavior is
mutation-tested (fix temporarily suppressed, confirmed the new test
fails, restored).

### v0.116.0 -- 2026-08-02

**Every TC18 SHOULD/MAY clause extracted and formally referenced, per a
direct follow-up instruction that verbal accounting wasn't enough.**
Grepped the full spec text for every SHOULD (12) and MAY (44) occurrence,
excluded 6 legal-boilerplate hits, and individually classified the
remaining 51. All 10 already-implemented optional capabilities the MAY
clauses describe got a real `tc18` citation added to their existing
requirement entry (`REQ-TRIG-001`, `REQ-TIMED-008`, `REQ-LIFECYCLE-001`,
`REQ-LIFECYCLE-004`, `REQ-PWRMODE-004`, `REQ-E2E-016`, `REQ-MOCK-019`,
`REQ-SEQ-001`, `REQ-MDIO-001`, `REQ-ADC-018`); the remaining 41
non-testable lines (design-goal prose, client-config-authoring advice,
hardware-deployment choices, non-closed-list permissions) are recorded
with individual citations in the new `docs/TC18-NON-NORMATIVE-CLAUSES.md`.
No code behavior changed; 1023 requirements, 100% traced+tested, 0
`cfusa check` errors.

### v0.115.0 -- 2026-08-02

**GPIO write requests never respected input-pin configuration.**
Investigating cpp-RCP's identical write-semantics bug (issue #105 there)
for a possible cross-repo instance found the same gap here: TC18
§13.7.4.3 states "a write request to an input pin is ignored for this
input pin," but `rcp_ep_gpio_apply_write()` -- the only write-application
primitive this library exposes -- has no notion of per-pin direction at
all, and nothing in this codebase wrapped it with masking; the function
was never wired into any dispatch path here either (its only callers are
its own unit tests), so this was a silent gap in the public API surface
itself rather than a wired-in behavioral bug.

Added `rcp_ep_gpio_apply_masked_write()`: computes the same combined
value `rcp_ep_gpio_apply_write()` would, then commits it only for bit
positions whose `pins[i]` has `RCP_REGMAP_PIN_PROP_OUTPUT` set, leaving
every input-configured bit unchanged regardless of what the request or
combinator result specify for it -- correct for every write semantics,
including a bare Replace, and a no-op for `RCP_EP_GPIO_WRITE_RECONFIG`
(which `rcp_ep_gpio_apply_write()` already leaves unchanged). The
existing `rcp_ep_gpio_apply_write()` is untouched, including its own 12
tests, which correctly exercise the unmasked combinator in isolation.

`REQ-GPIO-037` added -- 1023 requirements, 100% traced+tested, 0 `cfusa
check` errors. Purely additive (new function, no existing signature or
behavior changed) -- non-breaking.

### v0.114.0 -- 2026-08-02

**`rcp_acf_hdr_ack_has_event()` described a distinction TC18 does not
make (BREAKING).** Investigating this function's own TC18 basis (flagged
earlier and deferred) found it has no grounding in the spec: its doc
comment claimed to distinguish "a plain Acknowledge from one tagged with
an asynchronous event via evt," but TC18's own evt[3:0] field table
(§11.3, TC18.txt L1876-1880) defines exactly one Acknowledge encoding --
`0xF` -- with no room for a second, event-carrying variant; `0x0` is
simple/data/error response, `0x1-0x8` is a repetitive-response counter,
`0x9-0xE` is reserved, and none of those apply once evt[3:0] == 0xF has
already matched. Since `rcp_acf_classify_response()` only reaches the
Acknowledge classification via `hdr->evt == 0xF`, `evt != 0` was always
true for every real decoded Acknowledge -- the function's only way to
return false was through a hand-constructed header exercising the same
op==NONE fallback `rcp_acf_classify_response()`'s own doc comment already
documents as unreachable from real decoded input. All three of its tests
confirmed this: each one hand-built a header rather than decoding real
wire bytes. Removed the function, its declaration, `REQ-ACF-003`, and its
tests; corrected `rcp_acf_classify_response()`'s and the `evt` field's
doc comments to stop referencing it. No internal caller ever used this
function (grep-confirmed) -- 1022 requirements, 100% traced+tested, 0
`cfusa check` errors.

### v0.113.0 -- 2026-08-02

**Function-level requirement-coverage audit. Additive, no existing
behavior changed.** A user-requested deep audit asked whether every
public function has at least one traced requirement -- a bar `cfusa
check`/`trace` cannot itself verify (there is no rule for "function
lacks a requirement tag"; `trace`'s 100% gate only confirms every
*catalogued* requirement has impl+test tags, not that every function has
a catalogued requirement in the first place).

A hand-written script cross-referencing every function actually
*declared* in `include/rcp/*.h` against `//cfusa:req`-tagged definitions
in `src/*.c` found 39 genuine gaps (an earlier, cruder pass over all
function definitions regardless of visibility produced 223 false
positives by also flagging internal `static` helpers -- e.g. `acf.c`'s
own `put_u64`/`get_u64` byte-order helpers -- which correctly never get
individual requirements; narrowing to the real public-API surface
brought that down to the true number). All 39 are satellite/
infrastructure modules (`admin.c`, `authz.c`, `config.c`, `deadline.c`,
`e2e.c`, `faultinject.c`, `loan.c`, `mdns.c`, `mock.c`, `observe.c`,
`powerstate.c`, `ratelimit.c`, `recorder.c`, `server.c`, `tsn.c`,
`watchdog.c`) -- the TC18 protocol core (`acf.c`, every `ep_*.c`,
`request_*.c`) was already fully covered.

Every one of the 39 was verified to already have real test exercise
before being tagged (constructors/destructors used pervasively as
setup/teardown, or genuinely asserted-on return values) -- confirmed via
a second script cross-referencing function names against every test
file. Two real, previously-untested behaviors were found along the way
and given dedicated new tests rather than just a tag on an incidental
usage: `rcp_authz_policy_retain()`'s refcount contract (mutation-tested
under ASan -- a broken refcount reproduces as a genuine
heap-use-after-free, not just a wrong return value) and
`rcp_in_memory_sink_spans()`'s cap-truncation behavior (also
mutation-tested -- removing the bound corrupts memory on the very next
call). `REQ-ADMIN-009/010`, `REQ-AUTH-009-011`, `REQ-CFG-013`,
`REQ-DL-009-013`, `REQ-E2E-043/044`, `REQ-FI-011/012`, `REQ-LOAN-009`,
`REQ-MDNS-010/011`, `REQ-MOCK-027`, `REQ-OBS-014-019`, `REQ-PWR-011-015`,
`REQ-RL-010/011`, `REQ-REC-012-014`, `REQ-SRV-021`, `REQ-TSN-008`,
`REQ-WDG-011/012` added -- 1023 requirements, 100% traced+tested.

Verifying the above surfaced a second, independent gap: the locally
cached `cfusa` binary used for the preceding several releases' `check`/
`trace` runs predated this repo's own pinned c-FuSa `v0.5.50` tag by
several commits and was silently stale, producing false-clean results.
A rebuild from the exact CI-pinned tag turned up three genuinely
untraced requirements the stale binary had missed --
`REQ-MDNS-007`/`REQ-MDNS-008` (the `rcp_mdns_announcer_t` interface and
its `withdraw()` wrapper, both header-only with no `.c` implementation
to carry the tag) and `REQ-RELAY-013` (the `RCP_SPEC_VERSION` alias
macro) -- each already had real test exercise (`test_mdns.c`'s
`TestAnnouncer` double, `test_adapt.c`'s spec-version equality check)
but no `//cfusa:req`/`//cfusa:test` tag pinned to the specific
construct. Tagged all three in place. Also renamed
`tests/l2_veth_roundtrip.c` to `tests/l2_veth_roundtrip_test.c` (CMake
target and CI job untouched) so `cfusa trace --func-coverage`'s
test-file exemption -- which only recognizes the `test_*`/`*_test.c`
naming patterns already used by every other test file in this repo --
correctly excludes its `main()` from the public-function-annotation
count, the same way every other test harness in this codebase already
is. `cfusa check` now reports 0 errors and `cfusa trace --req-coverage
100 --func-coverage 100 --sec-tested 100` passes clean against the
correct tool version.

### v0.112.0 -- 2026-08-02

**LIN's evt[2:0] comparison scheme was invented, not spec-derived
(BREAKING).** `ep_lin.h`'s own file header admitted `rcp_ep_lin_compare_mode_t`
(an eight-value EXACT/PREFIX/ANY/NEVER+4-reserved enum) was "this module's
own original design... rather than on any spec-derived enumeration --
there being no such enumeration cited by the roadmap to derive one from."
There is one. TC18 §13.5 Table 30 places LIN in the exact same
`{ADC, PWM_IN, I2C, LIN, CAN, UART, ISELED, MDIO}` plain-request row every
other endpoint type's decoder already enforces via `rcp_acf_evt_row2_is_plain()`
(pixel-verified against the rendered specification page, same table this
session's ADC/I2C/UART/ISELED/MDIO/CAN fixes already established) -- LIN is
not called out as an exception anywhere in it. §13.7.10.1's own prose ("the
LIN endpoint checks each received message against the byte_msg_payload and
if a match under the conditions given by evt[2:0] is found a reply is
sent") describes the same universal §13.5.1 vocabulary every other
endpoint's compound-wait comparison uses, not a LIN-private multi-mode
scheme: since Table 30 constrains a plain LIN request's evt[2:0] to 000b,
the only comparison that can ever apply is §13.5.1 mode 000b, exact match.

**What changed.** `rcp_ep_lin_compare_mode_t`/`rcp_ep_lin_compare_valid()`/
`rcp_ep_lin_compare_fires()` removed. `rcp_ep_lin_encode_command_request()`
no longer takes a `compare_mode` parameter (always encodes `evt = 0`).
`rcp_ep_lin_decode_command_request()` no longer surfaces a compare mode;
it instead returns the new `RCP_EP_LIN_ERR_BAD_EVT` when `evt[2:0] != 000b`,
per Table 30's own UNSUPPORTED_CMD rule. New `rcp_ep_lin_response_matches()`
delegates directly to acf.h's shared `rcp_acf_compound_wait_match(0, ...)`
rather than reimplementing exact-match comparison logic of its own --
the same single-source-of-truth reuse this module's compound-wait
dispatch (v0.110.0/v0.111.0) already established. `adapt.c`'s
`RCP_ADAPT_OP_LIN_COMMAND` mapping drops the now-meaningless
`rcp.lin.compare_mode` metadata key.

**A second finding, resolved as a side effect.** `REQ-UART-035` tracked
"c-RCP provides no UART compound-wait comparison surface" as
not-implemented. It already was, as of v0.110.0/v0.111.0: acf.h's shared
primitive applies to every endpoint type uniformly, UART included, wired
through `server.c`'s `current_status`. §13.7.8.1's own "compared length
bounded above by uart_rx_fifo_size" falls directly out of the shared
§13.5.1 length rule once a real fifo's contents (which can never exceed
uart_rx_fifo_size) are supplied as `current_status` -- no UART-specific
logic needed. Marked implemented; its stale deviation-pinning test
(which referenced the now-removed LIN helper) replaced with a real
conformance test.

`REQ-LINEP-001` through `005` (the invented enum) removed; `REQ-LINEP-016`
through `018` corrected; `REQ-LINEP-025` through `027` added for the new
behavior. 100% traced.

### v0.111.0 -- 2026-08-01

**Wire the v0.110.0 compound-wait comparison primitive into real dispatch
(BREAKING).** v0.110.0 added the TC18 §13.5.1 comparison rule but left it
completely unreachable: `rcp_compound_encode_request()` had no way to set
`evt` (every compound-wait request silently encoded `evt = 0`, forcing
exact-match regardless of caller intent) and `rcp_compound_decode_request()`
discarded the decoded `evt` entirely. This release closes that gap end to
end.

**API changes (BREAKING):**
* `rcp_compound_encode_request()` gains an `evt` parameter.
* `rcp_compound_decode_request()` gains an `out_evt` output parameter.
* `rcp_server_tick_ctx_t`'s single `wait_condition_met` bool is replaced
  by `current_status`/`current_status_len`: the endpoint's own current
  status bytes. This was a real, separate defect, not just a signature
  change -- a single flat bool cannot distinguish between two
  simultaneously-pending COMPOUND_WAIT requests on the same endpoint with
  different `byte_msg_payload` targets; the old design would apply
  whichever single result the caller computed to *every* pending
  COMPOUND_WAIT request indiscriminately. `rcp_server_pending_t` now
  stores each COMPOUND_WAIT request's own `evt` and an owned copy of its
  `byte_msg_payload`, admitted and validated independently
  (`rcp_acf_compound_wait_evt_valid()` rejects the reserved `011b` value
  at admission time, per TC18 §13.5.1's own "ignored, err-response
  UNSUPPORTED_CMD" rule, rather than storing a request that could simply
  never match), and evaluated independently against the shared
  `current_status` at every tick.
* `rcp_ep_spi_compound_wait_status_equal()` and
  `RCP_EP_SPI_COMPOUND_WAIT_COMPARE_LEN` are removed: both modeled the
  comparison length as an SPI-specific hardcoded 4 bytes, which was never
  a real rule -- TC18's own length-capping rule (status capped to
  `byte_msg_payload`'s own length) is universal, and the specification's
  4-of-20-bytes worked example merely illustrates it using SPI.
  `RCP_EP_SPI_STATUS_MAX_LEN` (a genuinely SPI-specific status-report
  width bound, unrelated to compound-wait) is unaffected. `REQ-SPI-031`/
  `032` removed with it.

`REQ-CMP-026`/`027` (the encode/decode threading), `REQ-SRV-019`/`020`
(admission storage + independent per-request evaluation) added, 100%
traced. New test coverage includes two simultaneously-pending
COMPOUND_WAIT requests with distinct targets executing independently,
and reserved-`evt` admission rejection -- both mutation-tested against
the pre-fix behavior.

### v0.110.0 -- 2026-08-01

**TC18 §13.5.1 compound-wait comparison primitive. Additive, no existing
behavior changed.** A compound-wait request's `evt[2:0]` carries a
completely different meaning than Table 30's per-endpoint-type rule: it
selects one of eight ways to compare that request's own `byte_msg_payload`
against the addressed endpoint's current status, and this rule is
identical across every endpoint type. Nothing in this codebase implemented
it -- the two existing partial helpers this module now supersedes
(`rcp_ep_spi_compound_wait_status_equal()`, `rcp_ep_pwm_in_compound_wait_compare()`)
were never wired to a decoded request's `evt` at all, and neither
implements more than a handful of the eight modes.

**What was added.** `rcp_acf_compound_wait_evt_valid()` and
`rcp_acf_compound_wait_match()` (`acf.h`/`acf.c`), operating on raw
`payload`/`status` byte buffers per the specification's own wording:
exact match (000b), AND-with-1s-mask and AND-with-0s-mask (001b/010b),
reserved (011b, `UNSUPPORTED_CMD`), and four leading-quadlet high/low-word
`>=`/`<=` comparisons (100b-111b) -- including the length-capping rule the
specification states using its own SPI example ("only the first four out
of 20 received bytes will be checked"). Seven new requirements,
`REQ-ACF-024` through `REQ-ACF-030`, each independently hand-derived and
mutation-tested against the rendered specification page.

**What this does not yet do.** This primitive is not yet wired into
`rcp_compound_encode_request()`/`rcp_compound_decode_request()` (which
still cannot set/surface `evt` at all -- every compound-wait request is
currently encoded with `evt = 0`) or into `server.c`'s dispatch pipeline.
That wiring, and the removal of the two superseded endpoint-specific
helpers, is tracked as immediate follow-up work.

### v0.105.0 -- 2026-08-01

**Requirements-corpus completeness pass. No behavior change** -- not one
line of `src/` changes here. What changes is what `.fusa-reqs.json`
*claims*: it stops being a map of this implementation and starts being a
map of TC18's normative surface, with the parts this implementation does
not cover marked as such instead of simply absent.

**The problem.** A spec-coverage-gap audit read TC18 §10 through
§13.7.13 clause by clause and checked each MUST/shall sentence, each
normative table row and each figure-defined layout against all 817
existing catalog entries. Roughly a hundred and thirty mandatory clauses
had *no* corresponding entry at all -- not a wrong requirement, a missing
one. A catalog that only describes what a codebase does cannot show what
it does not do, and a reader had no way to tell "c-RCP implements TC18
§12.5" from "nobody has looked at §12.5". The concentrations were §12.5
(Goto Sleep / Goto Standby), §12.7.5-12.7.9 (the RC Server register map,
request-stream config, EP_ID map, response-stream config), §13.6 (E2E
safe points), and the per-endpoint functional-configuration and
trigger-signal tables of §13.7.

**What was added.** 158 entries, 817 -> 975, each carrying two new
fields:

* `"tc18"` -- the exact section, table or figure it was read from, plus
  the line of the specification text it was extracted from.
* `"status"` -- `"implemented"` (9), `"partial"` (62) or
  `"not-implemented"` (87).

`"implemented"` entries keep `scope: "tc18"` and ASIL-B: they describe
behavior this library provides and were simply never written down.
`"partial"` and `"not-implemented"` entries get a new `scope:
"tc18-gap"`, level/asil `QM`, and text that opens with `NOT
IMPLEMENTED:` and says plainly what the specification requires, what
c-RCP does instead, and what the consequence is. They are deliberately
**not** part of the ASIL-B safety-case basis -- claiming ASIL-B integrity
for absent behavior would be the same dishonesty in the other direction.

Every one of the 158 is traced by a `//cfusa:req` tag in its module's
public header and by a `//cfusa:test` tag in one of five new test files
(`tests/test_tc18_gaps_regmap.c`, `_server.c`, `_ep.c`, `_ep2.c`,
`_e2e.c`). The
tests for a gap entry are **deviation-pinning**, not decorative: they
assert the behavior the code actually has where it differs from the
clause, so the deviation is locked in as tested fact and a future fix
must update the requirement rather than silently drift. For example
`rcp_ep_i2c_mode_valid(4)` is asserted to be *false* while TC18 Table 46
defines Ultra-fast mode (5 Mbit/s) at value 4.

**Two corrections to existing entries.** `REQ-E2E-003` claimed the CRC
covers `avtp_timestamp` as "8 bytes, big-endian"; `src/e2e.c` has always
serialised a `uint32_t` with `put_u32` and `tests/test_e2e.c` has always
asserted 4 octets. The AVTPDU field is 32 bits. The requirement text was
wrong, not the code, and is corrected here.

**Known gaps this pass does *not* close.** §11.2.2.1-§11.2.3.3 (the
conditional/cancellation request families), §11.3-§11.4 (responses,
acknowledges, timestamps), §12.8.2 (frame reception), §12.9 (request,
response and error handling), §13.2/§13.3/§13.5 (the generic endpoint
register map, request validation, evt-bit usage) and the remainder of
§13.7.8-§13.7.13 still have roughly a hundred further candidate gaps
from the same audit that did not fit this pass. They are the next
increment, not a claim of completeness.

A capacity note for whoever writes that next increment: c-FuSa's
`cmd_trace.c` has a compile-time `MAX_REQS` of 1024 and silently ignores
requirements past it. At 975 this catalog is 49 entries from that
ceiling, so the limit needs raising upstream before the corpus grows
much further.

### v0.104.0 -- 2026-07-31

**BREAKING (wire format, and API).** Resolves the I2C transfer-direction
question v0.103.0 left open — but *not* in the direction that entry
guessed at. `ep_i2c.c` does **not** have LIN's and SPI's inverted-`op`
defect. It has a different one: `op` was a *constant* where it should be
a *parameter*.

**What the specification actually says.** §12.9.1 defines the two `op`
senses ("A response with pay load data read from the EP is given, if
requested by `op=0` (read request)"; "A response with `err=0` and no
payload is given after successful execution of a request with `op=1`
(write request)"), and §11.3.2/§11.3.3 mirror that split on the response
side — a write response "does not have a `byte_msg_payload`", a read
response does. §13.7.7.3 says only that the I2C payload "is the I2C
payload including the address" and that "the endpoint is just
transparent", and its **Figure 29 leaves the `op` cell blank** while
spelling the *I2C-bus-level* direction bit out inside the payload
instead: the first payload octet reads `1 1 1 1 0 A10 A9 RW`, the
10-bit-address prefix with its R/W bit. So the specification deliberately
does not pin `op` for this endpoint type — the R/W bit is a *payload* bit
the transparent endpoint clocks onto the bus, and `op` is the separate
RCP-level question of what response comes back.

That is why this is not LIN's or SPI's defect. Those two endpoints are
unconditionally response-bearing (a LIN command always asks for what came
back; an SPI transfer is full duplex and always returns POCI octets) and
their own sections pin `op=0` explicitly — §13.7.10.1's "a reply is sent
if `op = 0`", Figure 23's literal `op=0 ... read_size = 0x0A`. A constant
`op` is correct for them, and v0.103.0 corrected which constant. An I2C
transfer is **half duplex and genuinely either-directional**, so *no*
constant is correct for it. §13.7.4's GPIO wording confirms the general
model has all three shapes — "A read request without a `byte_msg_payload`
(pure read)", "A read request with a `byte_msg_payload` as well as a
write request …" — and a payload-bearing read request is exactly what an
I2C read is: address out, data back.

**The defect.** The module hard-coded the *write* sense on every request
and rejected the read sense outright as `RCP_EP_I2C_ERR_WRONG_OP`, so an
I2C read transaction — the very direction the payload's own R/W bit exists
to express — could be neither encoded nor accepted. Nor could a read
request carry a `read_size`: the header slot that says how many octets to
clock back was left 0, the same "asking a conforming endpoint for
nothing" shape v0.103.0 fixed in ADC. Meanwhile the module *did* offer a
data-bearing response encoder — which, per §12.9.1, only an `op=0` request
can lawfully elicit, and which it hard-coded to `op=0` even for the
response to a write, so a write confirmation classified as a read
response on the wire.

**The fix.** `rcp_ep_i2c_dir_t` makes the RCP-level direction an explicit
parameter of both codecs. A read request encodes `op=0` and carries
`read_size`; a write request encodes `op=1` and leaves that slot 0
(there it is a `segment_num`, not a `read_size`, so a `read_size` on a
write is now rejected at encode rather than silently mis-encoded). The
request decoder accepts both senses and reports which, instead of
rejecting half of them. `rcp_ep_i2c_encode_response()` takes the same
direction and encodes the matching response class, with a write
response's payload required to be empty. `RCP_EP_I2C_ERR_WRONG_OP` is
retained for source compatibility but is no longer produced: there is no
longer a "wrong" `op` on an I2C transfer.

`adapt.c`'s `RCP_ADAPT_OP_I2C_TRANSFER` mapping gains `rcp.i2c.read_size`
(absent or 0 = the write direction) on the request side and reports it
back on the response side, matching how `rcp.uart.read_size` and
`rcp.adc.read_size` already work.

**Verification method.** Read against the specification PDF directly, not
by analogy with the LIN/SPI pass: §12.9.1, §11.3.2/§11.3.3, §13.5's
Table 30, §13.7.4's request-shape wording, the byte-message-info field
tables' "if `op = 0` this is `read_size`, else `segment_num`", and
Figure 29 rendered from the PDF at 600 dpi to confirm the `op` cell is
genuinely empty rather than merely lost in text extraction. Full `ctest`
59/59 passing. The new tests quote each sentence they rest on and assert
the literal wire bit rather than re-encoded output.

`.fusa-reqs.json` rewrites `REQ-I2C-010` through `REQ-I2C-015`, which
described the constant-`op` behaviour as correct, and adds `REQ-I2C-017`
(`rcp_ep_i2c_dir_valid()`) and `REQ-I2C-018` (encode-time direction and
`read_size` validation).

### v0.103.0 -- 2026-07-31

**BREAKING (wire format, and API).** Six independently-confirmed
conformance defects across five endpoint modules, each re-verified
against the specification's own normative text before being changed.
None of them overlap; they are batched because they are unrelated
one-module-each fixes.

**1. PWM_OUT/GPIO `evt[2:0] = 110b` computed the subtraction backwards.**
The single table row that defines this operation covers the GPIO and
PWM_OUT endpoint group jointly and states it as *payload minus current
interface status*. Both modules computed *current minus payload*. Since
subtraction is not commutative every such request produced a different
value than a conforming peer expects (and, with saturation at 0x0000,
frequently produced 0 where a real value was due, or vice versa). The
row's parenthetical remark about decreasing a PWM duty cycle is an
illustrative note, not a second definition of the operand order -- GPIO
was corrected on the same normative sentence, not by analogy.

**2. PWM_OUT `evt[2:0] = 111b` was a single enable-toggle bit; it is an
addressed register write.** The configuration escape hatch is defined as
a 16-bit big-endian relative start address followed by configuration
data written into the endpoint's own EP_func block. `ep_pwm.h` now models
that block -- EP_LEN, reserved, enable&clr, options, base clock, status,
clock divider, the output signal flags, duty-cycle min/max, skew -- with
`rcp_ep_pwm_out_apply_reconfig()` doing the real addressed write
(honouring the "a write extending past EP_LEN is ignored" rule and
leaving read-only registers alone), plus
`rcp_ep_pwm_out_render_registers()` and
`rcp_ep_pwm_out_encode_reconfig_request()`. `rcp_ep_pwm_out_functional_cfg_t`
loses its duplicate `enabled` bool: the endpoint's enable bit is the
EP-common one `regmap.h` already models, which is what the block's
enable&clr register carries.

**3. ADC `adc_combine_avg_values` was modelled as a four-way
AVERAGE/MIN/MAX/LATEST mode enum; it is an output-value COUNT.** The
register table defines the field as the number of output values to be
combined into one response, and a response carries as many measurement
values as half the request's `read_size`. Every ADC response this library
produced was exactly one 2-octet value wide, with the other averages
silently reduced away. The response codec is now genuinely multi-value:
`rcp_ep_adc_encode_response()`/`_decode_response()` carry N values and
report `2 * N` as `read_size`, `rcp_ep_adc_collect_response_values()`
replaces the reduction with a packing step, `rcp_ep_adc_response_value_count()`
expresses the read_size relationship, and
`rcp_ep_adc_encode_read_request()` now carries `read_size` at all (it
previously left it 0, so a conforming endpoint would have answered with
nothing).

**4. ADC response timestamp came from the wrong end of the averaging
window.** The rule is the moment the *last* sample feeding the *first*
averaged value in the response was captured; `rcp_ep_adc_average_interval()`
reported `samples[0].timestamp`, the interval's *first* sample -- wrong by
a whole averaging interval for any interval longer than one sample.

**5. Discovery's `svr_version` was 16 bit; the general register map
defines it as 32 bit.** Every field after it in the slice was therefore
two octets early on the wire, so a conforming client misparsed
`vendor_id`, `device_id` *and* `svr_ep_count` from every discovery
response this library sent. `RCP_DISCOVERY_GENERAL_SLICE_LEN` goes 12 ->
14, `rcp_regmap_general_t::svr_version` and
`rcp_discovery_result_t::svr_version` become `uint32_t`, and all four
encode/decode paths in `discovery.c` are corrected.

**6. LIN and SPI both encoded the `op` direction inverted.** `op=0` is
the read/reply-expected direction and `op=1` the write/no-data-response
one; the LIN endpoint's own reply rule is stated in terms of `op = 0`,
and the specification's worked SPI example ("write 20 bytes and get a
response with 10") carries `op=0` with a non-zero `read_size`. Both
modules encoded `op=1` on a request that expects data back, and rejected
the correct `op=0` as malformed -- so a conforming peer's request was
refused and this library's own was told no response was wanted. Both
modules' existing tests asserted the wrong direction as correct; they now
assert the literal wire bit with the specification text cited.

**Testing.** 59/59 `ctest` targets passing. Every corrected assertion
carries an explicit section/table/figure citation with the quoted
normative sentence, so the assertions are anchored to the specification
rather than re-derived from this library's own encoder. New coverage
includes the PWM_OUT EP_func register block (per-register offsets,
partial multi-octet writes, the EP_LEN overrun rule, read-only registers,
and a full request round trip), the ADC eight-measurement response
geometry and end-to-end sampling pipeline, and a discovery general-slice
octet-layout test pinning each field to its cited absolute address.

`.fusa-reqs.json` rewrites every requirement that described one of these
behaviours as correct: `REQ-GPIO-011`, `REQ-PWM-007`/`010`/`011`/`016`/`023`,
`REQ-ADC-001`/`005`..`012`/`014`/`022`/`023`/`025`/`027`..`030`,
`REQ-DISC-010`/`012`, `REQ-LINEP-016`/`018`, and `REQ-SPI-026`/`027`.

**Known, deliberately out of scope**: `ep_i2c.c`'s transfer request has
the same request-carries-`op=WRITE` shape LIN and SPI had, and may have
the same inversion; it was not part of this pass's verified set and is
left for its own change. *(Resolved in v0.104.0 — verified against the
specification directly and found to be a **different** defect, not the
same inversion. Declining to pattern-match it here was the right call.)*

### v0.102.0 -- 2026-07-31

**BREAKING (wire format, and API).** Fixes the entire conditional-request
feature area (TC18 §11.2.2/§11.2.3), which was broken at two independent
levels: the wire sub-field encodings were non-conformant, *and* none of
these request kinds were wired into the reference server's dispatch path
at all.

**Wire sub-field fixes**, each re-derived from the specification's own
figures and field tables:

| Request kind | Was | Now (TC18 v0.5.1_RC) |
|---|---|---|
| Compound / compound-wait `0x0F`/`0x8F`, `0x0B`/`0x8B` | 16-bit `sequencer` at offset 1; `start_state`/`next_state` two octets late; 8-bit repetitions with an `0xFF` infinite sentinel | offsets 1..7 = `start_state`(1) `next_state`(1) `sequencer`(1) `exec_delay`(2) `repetitions`(2), infinite sentinel `0xFFFF` (Fig. 8/Table 6, Fig. 9/Table 7) |
| Triggered `0x0E`/`0x8E` | compound's `sequencer`/`start_state`/`next_state` — no trigger-selection fields at all | `trigger_source_ep`(1) `trigger_signal_nr`(1) `trigger_threshold`(1) `exec_delay`(2) `repetitions`(2) (Fig. 10/Table 8) |
| Timed `0x0A` | 32-bit value at offset 1, overwriting the reserved octet | reserved-zero octet at offset 1, 48-bit big-endian `presentation_time` at offsets 2..7 (Fig. 12/Table 10) |
| Chained `0x01` | invented `chain_length`/`chain_position` over reserved octets; no `chain_exec_delay` | `chain_exec_delay`(2) at offsets 4..5, offsets 1..3 and 6..7 reserved-zero (Fig. 11/Table 9) |
| Clear-single `0x07` | `clear_transaction_num` at offset 1 | `clear_transaction_num` at offset 3 (Fig. 15/Table 13) |

Reserved octets are now *enforced* on decode, not merely written as zero:
`RCP_TIMED_ERR_RESERVED_NONZERO`, `RCP_CHAINED_ERR_RESERVED_NONZERO` and
`RCP_CANCEL_ERR_RESERVED_NONZERO` are new. Timed requests additionally
reject a set `hs`/`cs` (`RCP_TIMED_ERR_UNSUPPORTED_CMD`).

Two behavioral rules in the same area were wrong rather than absent, and
are corrected: chained's `cs` bit is a *conditional start* selector read
on the member about to run about its predecessor's outcome (it was read
off the member that errored, inverting who controls the abort), and
compound's `start_state == 0` ("start in any state") and `next_state == 0`
("remain in the current state") sentinels are now implemented.

**Dispatch integration.** `rcp_compound_tick()`, `rcp_compound_wait_tick()`,
`rcp_triggered_tick()`, `rcp_chained_advance()`, `rcp_timed_admit()` and
`rcp_cancel_attempt()` previously had no callers outside their own modules
and unit tests: `src/mock.c` never inspected `request_type`, and
`rcp_sched_compare()`'s priority ordering and
`rcp_e2e_request_may_execute()`'s safety gate were correct but never
invoked. Dispatch was unconditional FIFO and only standard requests
worked end to end. `server.h`/`server.c` now own a per-endpoint
**request store** (`rcp_server_endpoint_admit()`,
`rcp_server_endpoint_select_due()`, `rcp_server_endpoint_complete()`,
plus trigger notification, chain linkage, cancellation and watchdog
purge), and `mock.c` drives it (`rcp_mock_server_tick()`,
`rcp_mock_server_notify_trigger()`,
`rcp_mock_server_set_sequencer_count()`,
`rcp_mock_server_watchdog_purge()`, and chain sequencing across a frame's
members). Standard-request behavior is unchanged.

**API changes**: `rcp_compound_step_t.sequencer_index` is now `uint8_t`
and `.repeat_count` `uint16_t`, `.exec_delay_ms` renamed `.exec_delay`
(its unit is multiples of the endpoint's `ep_delay_time`, never
milliseconds); `rcp_triggered_step_t` replaces its sequencer fields with
the trigger-selection ones and `rcp_triggered_tick()` takes no sequencer
table; `rcp_timed_*` presentation times are `uint64_t`;
`rcp_chained_encode_member()`/`_decode_member()` take `chain_exec_delay`
instead of `chain_length`/`chain_position`; `rcp_chained_advance()` gains
a `has_predecessor` parameter. `RCP_CHAINED_MIN_MEMBERS`,
`RCP_CHAINED_ERR_TOO_FEW_MEMBERS` and
`RCP_CHAINED_ERR_POSITION_OUT_OF_RANGE` are removed.

**Still open**: `src/mock.c`'s dispatcher has no `ep_type` switch and
never calls any `ep_*.h` decode function for *standard* requests either —
it invokes only the caller-supplied handler. That broader gap is
untouched here.

New `tests/test_conditional_dispatch.c` (22 cases) exercises these kinds
end to end through the real dispatch path; each module also gains literal
octet-offset assertions written from the specification's figures rather
than copied out of the encoder (the pre-existing round-trip tests all
passed against the wrong layouts, since encode and decode agreed with
each other). `ctest` 59/59 passing. `include/rcp/version.h` had drifted
from `CMakeLists.txt` (`0.100.0` vs `0.101.0`); both, and `.fusa.json`,
are now `0.102.0`.

### v0.101.0 -- 2026-07-31

Adds the native-Ethernet (L2) `rcp_avtp_transport_t` carrier
(`rcp/l2.h`/`l2.c`) milestone 59 (v0.59.0) originally named as one of
three concrete transports this vtable was purpose-built to admit, and
that milestone 78 (v0.78.0) silently dropped without implementing.
Linux-only (`AF_PACKET`/`SOCK_RAW`, needs `CAP_NET_RAW`/root): frames are
destination MAC (6) + source MAC (6, read from the given interface via
`SIOCGIFHWADDR`, never caller-supplied) + EtherType `0x22F0` (2,
big-endian) + the AVTPDU directly. Every non-Linux build gets the same
fail-cleanly stub `udp.c`'s own Windows stub already established.

Also fixes a real Annex J conformance gap in the existing UDP transport,
present since v0.78.0: UDP/IP framing (Annex J) prepends a 4-octet,
big-endian encapsulation sequence number ahead of every AVTPDU on the
wire, which `udp.c` never implemented, and had no standard default
control-plane port (17221) either. Cross-checked against two independent
public secondary sources (a Wireshark issue tracker discussion and the
COVESA Open1722 reference implementation) rather than the paywalled IEEE
1722-2016 standard text itself, which this project has no access to --
both `udp.h`'s own file header and this entry say so explicitly. New
`rcp_udp_annexj_wrap()`/`_unwrap()` (pure, socket-free) are the codec,
applied transparently inside `rcp_udp_avtp_transport_dial()`/`_bind()`'s
existing `send()`/`recv()`; new `RCP_UDP_ANNEX_J_CONTROL_PORT` (17221)
and `_dial_default_port()`/`_bind_default_port()` convenience wrappers
fill it in, without changing port `0`'s existing, different, already-
tested meaning for `bind()` ("OS-assigned ephemeral port").

New `tests/test_l2.c` (frame codec, unprivileged, every platform) and 7
new `tests/test_udp.c` cases (encapsulation codec, monotonic send-
sequence observability, default-port wrappers) -- the existing UDP
round-trip tests needed no changes, since the new framing is applied/
stripped transparently. New, deliberately-not-`ctest`-wired
`tests/l2_veth_roundtrip.c` moves real AVTPDUs across two real
`rcp_l2_avtp_transport_t` instances over a real `veth` pair, verifying
byte-for-byte equality in both directions and a real
close()-unblocks-a-concurrent-recv() round trip; a new Linux-only,
elevated-privilege `ci.yml` job (`l2-transport-veth`) builds and runs it.
New `REQ-UDP-015`..`019` and `REQ-L2-001`..`010` added to
`.fusa-reqs.json`. Full local `ctest` suite 58/58 passing, clean under
`-fsanitize=address,undefined`.

### v0.100.0 -- 2026-07-31

BREAKING: fix `acf.h`/`acf.c`'s `byte_message_info` header wire layout,
found by a 2026-07/31 cross-repo external gap audit -- through v0.99.0
it was this implementation's own invented byte layout (self-documented
as such in `acf.h`'s own file header), never wire-compatible with a
real TC18 peer or with this project's own `go-RCP`/`cpp-RCP` ports.
Rewrote the header encode/decode from the specification's real Figure 7
/ Table 4 layout field by field, bit by bit: `acf_msg_type` (7 bits) and
`acf_msg_length` (9 bits, now a QUADLET count over the whole message,
not an octet count of the payload alone) share the first 16-bit word;
`byte_bus_id` is an 11-bit field split across two octets; `pad`/`mtv`/
`hs`/`cs`/`rsp`/`err`/`evt`/`op`/`ms` are regrouped into the
specification's own octet 2/4/6 layout. `mtv` is now a single wire bit
(dropped the invented `RCP_ACF_MTV_UNCERTAIN` third state);
`read_size_or_segment_num` widened `uint8_t` -> `uint16_t` for its full
12-bit range; a decoded byte_bus_id exceeding 255 now fails explicitly
(`RCP_ACF_ERR_BUS_ID_OVERFLOW`) instead of truncating silently; `op`'s
encode-only `RCP_ACF_OP_NONE` convenience value now encodes identically
to `RCP_ACF_OP_WRITE` (there is only one wire bit, not three states).
`RCP_ACF_MAX_PAYLOAD` drops from 65535 (the old, wrong 16-bit field) to
`RCP_ACF_GBB_MAX_PAYLOAD` (2028)/`RCP_ACF_ABB_MAX_PAYLOAD` (2036) --
consequently a worst-case CAN XL new-payload frame no longer fits a
single unfragmented ACF message; the existing fragmented *response*
path is unaffected and is now the only way to carry one, but there is
still no fragmented *request* path (tracked as a follow-up).

New shared `rcp_acf_pack_header()`/`rcp_acf_unpack_header()` are the
single source of truth for this bit-packing, now used by `acf.c` itself
and by the five request-kind modules (`request_compound.c`,
`request_cancel.c`, `request_timed.c`, `request_triggered.c`,
`request_chained.c`) that build a raw ACF_GBB header by hand to
repurpose `message_timestamp` -- each previously duplicated the (wrong)
bit-packing independently. `e2e.c`'s `adapt_acf_msg_length()` and
`scheduler.c`'s `rcp_sched_split_frame_members()` are updated to the new
field position.

Hand-verified against the specification's own Figure 19 (ACF_ABB,
header+6-byte payload+2-byte pad+4-byte CRC32 = 20 octets = 5 quadlets)
and Figure 20 (ACF_GBB, header+timestamp+7-byte payload+1-byte pad+
4-byte CRC32 = 28 octets = 7 quadlets) worked examples: both reproduce
exactly, pinned as golden-vector tests in `tests/test_acf.c`.

Separately, closed the TC18 §12.9.1.1 multi-request-per-frame gap:
added `rcp_mock_server_dispatch_frame()`, which splits a raw NTSCF/TSCF
payload into its constituent ACF messages (`scheduler.c`'s previously-
orphaned `rcp_sched_split_frame_members()`) and dispatches each to its
own addressed `byte_bus_id` via the existing single-request
`rcp_mock_server_dispatch()`.

Deferred (see `ROADMAP.md` milestone 100 for the full list): TC18's
numbered wire error codes (`errors.h`) still are not populated onto any
endpoint's actual Error response; `coverage-report.json`'s self-reported
FAIL and `qualify-report.json`'s inconsistent qualification signals are
both `cfusa`-generated artifacts whose correct fix belongs upstream in
`c-FuSa`, not as a hand-edit here. `ci.yml`'s `ilammy/msvc-dev-cmd@v1`
mutable-tag pin was SHA-pinned as a small, unrelated hardening fix
bundled into this release. `README.md`'s wire-interop claim now carries
an explicit hedge. (Milestone 100; see `ROADMAP.md` for full detail.)

### v0.99.0 -- 2026-07-30

Bump `ci.yml`/`release.yml`'s c-FuSa pin from v0.5.49 to v0.5.50.
v0.5.50's release notes disclosed that c-FuSa's shared ASIL-derivation
table had over-assigned ASIL in 19 of 36 S/E/C cells for its entire
history through v0.5.49; a real local v0.5.50 build surfaced 11 new
`HARA006` errors against this repo's own `.fusa-hara.json` as a direct
consequence -- all 11 recorded hazard ASIL letters had been computed
against the buggy table. Independently re-derived all 11 by hand
against the real ISO 26262-3:2018 Table 4 before accepting the
correction (matches `cfusa hara asil` v0.5.50 exactly for all 11).
This supersedes v0.97.0's "Re-verify HARA ASIL audit finding"
conclusion, which had re-verified the same values against the *old,
buggy* tool and found them self-consistent -- self-consistency with a
buggy tool was never the same as correctness. Corrected
`.fusa-hara.json`, `HARA.md`, `SAFETY_PLAN.md`, `AUDIT_PACK.md`, and
`tara.md`. Only one hazard (H-001) now exceeds the ASIL-B baseline,
at ASIL-C (previously recorded ASIL-D); H-003/H-005/H-008 now land
exactly at baseline; H-011 drops to QM. (Milestone 99; see
`ROADMAP.md` for full detail.)

### v0.98.0 -- 2026-07-30

Fix an out-of-bounds stack read in Prometheus metrics rendering
(NEW-C-01, found by a follow-up re-audit after v0.97.0). Each counter
line is formatted into a fixed 256-byte stack buffer via `snprintf`,
but the code then used `snprintf`'s return value -- the length that
*would* have been written, not the truncated length actually in the
buffer -- as the size passed to `memcpy`. A counter with a long enough
`name`/`labels` pair (up to 63/127 bytes each, embedded twice for
`name`) formats to ~297 bytes, so the `memcpy` read ~41 bytes past the
end of `line[]` into adjacent stack contents (CWE-125, reachable by
any long-enough registered counter; a real memory-safety defect in a
safety-certified C library). Fixed by clamping to the buffer's actual
capacity before copying, the same defensive pattern `cli.c` already
uses elsewhere in this codebase. Added a regression test
(`test_metrics_text_with_long_name_and_labels_does_not_read_out_of_bounds`)
with a maximal-length name/labels pair; confirmed it reproduces a
`stack-buffer-overflow` under ASan against the unfixed code and passes
clean against the fix. (Milestone 98.)

### v0.97.0 -- 2026-07-30

Fix a real HARA.md documentation bug found alongside a 2026-07-30
ecosystem audit finding that did not hold up on re-verification. The
audit claimed all 11 HARA.md hazards were mis-rated one-to-two ASIL
bands high vs. a simplified S+E+C summation. Re-derived every hazard
against the project's own `cfusa hara asil` tool (the actual ISO
26262-3:2018 Table 4 lookup, not a linear sum) and found all 11
current ASIL letters already correct -- the audit's summation
heuristic does not match the real, non-linear Table 4. No hazard's
ASIL rating changed. What *was* wrong, found during the same
re-verification: the ASIL Determination Note said "Four hazards
resolve to ASIL-C or ASIL-D" while actually naming five (H-001, H-003,
H-005, H-007, H-008 -- H-007 does compute ASIL-C, confirmed by the same
tool), and the Residual Risks table's summary row independently
repeated the same four-hazard undercount, both missing H-007. Fixed
both to say "Five" and list all five hazards. (Milestone 97; see
`ROADMAP.md` for full detail.)

### v0.96.0 -- 2026-07-30

Add `/* SPDX-License-Identifier: MPL-2.0 */` as the first line of every
first-party `.h`/`.c` file (`include/rcp`, `include/relay`, `src`,
`cli`, `tests` -- 178 files), closing a gap the 2026-07-30 ecosystem
audit found: this repo ships per-version SPDX SBOMs making per-file
license claims with no source-level tag to cross-check them against.
Added a new `spdx-headers` CI job enforcing the header on every such
file going forward, per the audit's "enforce via CI lint"
recommendation (no such rule existed in the c-FuSa toolkit itself, so
this is a small dedicated grep-based check). (Milestone 96; see
`ROADMAP.md` for full detail.)

### v0.95.0 -- 2026-07-30

CI hardening: two masked/missing gates found by the 2026-07-30
ecosystem audit are now real. Added a `sanitizers (ASan/UBSan)` job
(c-RCP-N2-04): builds and runs the full test suite with
`-fsanitize=address,undefined`, hard-gated (no `|| true`) -- this
codebase parses untrusted network frames with unchecked pointer
arithmetic throughout, which static lint alone cannot prove free of
out-of-bounds reads or integer-overflow UB. Split the DO-178C
structural-coverage step (c-RCP-N2-05) into two: the original DAL-B
report generation stays best-effort (DAL-B's 100%/100% target isn't
met yet -- tracked separately, c-RCP-11), but a new "Coverage
regression gate" step now hard-fails if line coverage drops below 88%
(comfortably under the ~94% currently measured, leaving toolchain
variance margin) -- previously nothing enforced any coverage floor at
all. (Milestone 95; see `ROADMAP.md` for full detail.)

### v0.94.0 -- 2026-07-30

Fix three CLI `capabilities` self-report accuracy issues found by the
2026-07-30 ecosystem audit. Dropped `tls` from `transports`: `tls.h`/
`tls.c` were removed outright at v0.78.0 (see this file's Deprecation &
Removal Log), so the self-report was advertising a transport backend
that no longer exists in the tree at all. `RCP_CLI_ALL_IMPLEMENTED_
OPTIONS` (a single hardcoded "every bit in all three groups" constant,
despite its own comment claiming it named "one per group this build
actually implements") is now `RCP_CLI_IMPLEMENTED_OPTIONS`, computed
from three independently-named per-group predicates. Independently
verified against the actual code that none of the three feature groups
(`time_sync`, `enhanced_cancel`, `compound_bundles`) is fully
TC18-wire-conformant yet, despite each having real, working API surface
compiled in (`request_timed.c`'s `presentation_time` is a plain
`uint32_t`; `request_cancel.c`'s encoders hard-code the ACF header's
`evt` sub-field to 0; `request_compound.c`'s `repeat_count` is an
8-bit, round-tripped-only sub-field with no repetition state machine)
-- documented this "API surface, not wire conformance" caveat directly
in `cli.c` and `README.md`, since the RELAY capabilities JSON schema's
`features` array (`additionalProperties: false`, flat `string[]`) has
no way to carry a per-entry caveat on the wire itself. (Milestone 94;
see `ROADMAP.md` for full detail.)

### v0.93.0 -- 2026-07-30

Add the numbered TC18 wire error-code table (`rcp/errors.h`,
`rcp_wire_error_t`), which this project previously shipped no
representation of at all -- only local `rcp_errc_t`/module-local
`rcp_<mod>_errc_t` enums, none of which model the numbered codes an RC
Server places in a Response frame's err field. Also fixes `src/e2e.c`'s
`rcp_e2e_strerror()`, which hardcoded the spec's own prose alias
"CRC_ERROR" for a CRC mismatch -- a name with no entry in the spec's
authoritative numbered table, which instead assigns a CRC mismatch the
code POCI_FAILURE (12). Added `rcp_e2e_wire_error()`, mapping
`RCP_E2E_ERR_CRC_MISMATCH` to `RCP_ERROR_POCI_FAILURE` (and everything
else to a new `RCP_ERROR_NONE` sentinel for "no applicable wire code").
(Milestone 93; see `ROADMAP.md` for full detail.)

### v0.92.0 -- 2026-07-30

BREAKING: fix two E2E CRC32 wire-conformance defects found by the
2026-07-30 ecosystem audit. `rcp_e2e_compute_crc()` (and the
`rcp_e2e_wrap()`/`rcp_e2e_unwrap()` pair built on it) folded
`avtp_timestamp` into the CRC as 8 octets; it is a 4-octet field on the
wire (matching `avtp.h`'s own `uint32_t` modeling of it), so every
E2E-protected frame carried 4 extra always-zero octets no conformant
peer would include -- no safe-command-mode CRC produced by this
implementation could validate against one, or vice versa. Changed the
`avtp_timestamp` parameter on all three functions from `uint64_t` to
`uint32_t`.

Separately, `rcp_e2e_wrap()` never performed the mandated "adapt
acf_msg_length by plus one quadlet before computing the CRC" step:
it appended the trailer to an already-encoded ACF frame without
touching the acf_msg_length field baked into that frame's header, so
the declared message length under-counted the trailer by 4 octets --
wrong for any peer relying on acf_msg_length to find message
boundaries in a concatenated ACF stream. `rcp_e2e_wrap()` now adapts a
copy of the given frame's acf_msg_length (offset 1-2 per acf.h's
documented layout) by +1 quadlet before computing the CRC and
appending the trailer; `rcp_e2e_unwrap()` reverses both steps and
returns an owned copy (previously a borrowed pointer into the input)
ready to hand to acf.c's decoders unmodified.

(Milestone 92; see `ROADMAP.md` for full detail.)

### v0.91.0 -- 2026-07-30

BREAKING: remove the retired pre-TC18 placeholder protocol residue --
`wire.h`/`wire.c` (the `'RC'`-magic codec), `sim.h`/`sim.c` (the
zone-controller simulator), the `rcp_zone_t`/`rcp_command_t`/
`rcp_response_t`/`rcp_status_t`/`rcp_controller_t`/`rcp_registry_t`
object model in `rcp.h`/`rcp.c`, the `tests/legacy_mock.*` double and its
six placeholder-certifying test/benchmark targets, ~90 dead
`REQ-ZONE`/`REQ-PRI`/`REQ-CMD`/`REQ-STATUS`/`REQ-CMDSTRUCT`/`REQ-RESP`/
`REQ-STAT`/`REQ-CTRL`/`REQ-REG`/`REQ-SIM`/`REQ-ERR-*` requirement
entries, and the orphaned `tooling/zone_manifest_schema.json`. Per RELAY
spec §15.5 this ships with no compatibility shim. `rcp.h`/`rcp.c` retain
only the protocol-agnostic primitives every TC18 module actually shares
(`rcp_bytes_t`, the base `rcp_errc_t` sentinels, `rcp_context_t`,
`RCP_SPEC_VERSION`) -- reworked as new `REQ-CORE-*` requirements, since
their old `REQ-CTRL-026`/`REQ-CTRL-027`/`REQ-STAT-004` tags described
retired-model-specific copy semantics. Also fixes two generic-primitive
requirement families (mutex/condvar/thread in `platform.c`, the
monotonic clock in `clock.c`) that had been coincidentally tagged with
soon-to-be-deleted `REQ-CTRL-*` IDs despite implementing unrelated,
codebase-wide infrastructure -- retagged as new `REQ-PLATFORM-*`
requirements with dedicated test coverage (`tests/test_platform.c`).
(Milestone 91; see `ROADMAP.md` for full detail.)

### v0.90.0 -- 2026-07-29

Add CHANGELOG.md. (Milestone 90; see `ROADMAP.md` for full detail.)

### v0.89.0 -- 2026-07-29

RELAY_SPEC_VERSION tracks spec v2.0. (Milestone 89; see `ROADMAP.md` for full detail.)

### v0.88.0 -- 2026-07-29

cfusa-trace CI job now gates on --sec-tested too. (Milestone 88; see `ROADMAP.md` for full detail.)

### v0.87.0 -- 2026-07-29

Pin c-FuSa to v0.5.49. (Milestone 87; see `ROADMAP.md` for full detail.)

### v0.86.0 -- 2026-07-29

GPIO/PWM_OUT write-semantics enum off-by-one. (Milestone 86; see `ROADMAP.md` for full detail.)

### v0.85.0 -- 2026-07-28

Full re-certification pass. (Milestone 85; see `ROADMAP.md` for full detail.)

### v0.84.0 -- 2026-07-28

RELAY adapter rework. (Milestone 84; see `ROADMAP.md` for full detail.)

### v0.83.0 -- 2026-07-28

Deprecation batch. (Milestone 83; see `ROADMAP.md` for full detail.)

### v0.82.0 -- 2026-07-28

Optional discovery convenience. (Milestone 82; see `ROADMAP.md` for full detail.)

### v0.81.0 -- 2026-07-28

Protocol bridges. (Milestone 81; see `ROADMAP.md` for full detail.)

### v0.80.0 -- 2026-07-28

Generic decorators, batch 1. (Milestone 80; see `ROADMAP.md` for full detail.)

### v0.79.0 -- 2026-07-28

Safety-adjacent satellites. (Milestone 79; see `ROADMAP.md` for full detail.)

### v0.78.0 -- 2026-07-28

Transport satellites. (Milestone 78; see `ROADMAP.md` for full detail.)

### v0.77.0 -- 2026-07-28

Foundational test/config satellites. (Milestone 77; see `ROADMAP.md` for full detail.)

### v0.76.0 -- 2026-07-28

Fragmentation support. (Milestone 76; see `ROADMAP.md` for full detail.)

### v0.75.0 -- 2026-07-28

Wakeup control endpoint + power modes. (Milestone 75; see `ROADMAP.md` for full detail.)

### v0.74.0 -- 2026-07-28

MDIO endpoint. (Milestone 74; see `ROADMAP.md` for full detail.)

### v0.73.0 -- 2026-07-28

ISELED endpoint. (Milestone 73; see `ROADMAP.md` for full detail.)

### v0.72.0 -- 2026-07-28

CAN controller endpoint, incl. CAN XL. (Milestone 72; see `ROADMAP.md` for full detail.)

### v0.71.0 -- 2026-07-28

LIN commander endpoint. (Milestone 71; see `ROADMAP.md` for full detail.)

### v0.70.0 -- 2026-07-28

Safe points: CRC32 + safety-request variants. (Milestone 70; see `ROADMAP.md` for full detail.)

### v0.69.0 -- 2026-07-28

Triggered/chained/timed requests + cancellation taxonomy. (Milestone 69; see `ROADMAP.md` for full detail.)

### v0.68.0 -- 2026-07-28

Compound + compound-wait requests + sequencers. (Milestone 68; see `ROADMAP.md` for full detail.)

### v0.67.0 -- 2026-07-28

ADC + PWM_OUT + PWM_IN endpoints. (Milestone 67; see `ROADMAP.md` for full detail.)

### v0.66.0 -- 2026-07-28

I²C + UART endpoints. (Milestone 66; see `ROADMAP.md` for full detail.)

### v0.65.0 -- 2026-07-28

SPI endpoint. (Milestone 65; see `ROADMAP.md` for full detail.)

### v0.64.0 -- 2026-07-28

GPIO endpoint. (Milestone 64; see `ROADMAP.md` for full detail.)

### v0.63.0 -- 2026-07-28

Discovery. (Milestone 63; see `ROADMAP.md` for full detail.)

### v0.62.0 -- 2026-07-28

Register-map model. (Milestone 62; see `ROADMAP.md` for full detail.)

### v0.61.0 -- 2026-07-28

Lifecycle state machine. (Milestone 61; see `ROADMAP.md` for full detail.)

### v0.60.0 -- 2026-07-28

ACF message format + byte_message_info header. (Milestone 60; see `ROADMAP.md` for full detail.)

### v0.59.0 -- 2026-07-28

AVTPDU framing. (Milestone 59; see `ROADMAP.md` for full detail.)

### v0.58.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 5: real TLC verification. (Milestone 58; see `ROADMAP.md` for full detail.)

### v0.57.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 4: real TARA content. (Milestone 57; see `ROADMAP.md` for full detail.)

### v0.56.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 3: Adapt() error wrapping. (Milestone 56; see `ROADMAP.md` for full detail.)

### v0.55.0 -- 2026-07-27

RELAY-conformance audit remediation, batch 2: mechanical fixes. (Milestone 55; see `ROADMAP.md` for full detail.)

### v0.54.0 -- 2026-07-27

Coverage maximization, batch 4: internal helpers. (Milestone 54; see `ROADMAP.md` for full detail.)

### v0.53.0 -- 2026-07-27

Coverage maximization, batch 3: registry lifecycle. (Milestone 53; see `ROADMAP.md` for full detail.)

### v0.52.0 -- 2026-07-27

Coverage maximization, batch 2: decorator vtable methods. (Milestone 52; see `ROADMAP.md` for full detail.)

### v0.51.0 -- 2026-07-27

Fix: branch coverage still not captured on real CI. (Milestone 51; see `ROADMAP.md` for full detail.)

### v0.50.0 -- 2026-07-27

Coverage maximization, batch 1: branch instrumentation + strerror() functions. (Milestone 50; see `ROADMAP.md` for full detail.)

### v0.49.0 -- 2026-07-27

CI hardening: remove vestigial CFUSA-L004 escape hatch. (Milestone 49; see `ROADMAP.md` for full detail.)

### v0.48.0 -- 2026-07-27

`relay conform` CI gate -- closes #12. (Milestone 48; see `ROADMAP.md` for full detail.)

### v0.47.0 -- 2026-07-27

CLI binary — `version`/`capabilities`/`status` -- closes #8. (Milestone 47; see `ROADMAP.md` for full detail.)

### v0.46.0 -- 2026-07-27

`Adapt()`/`ToMessage()`/`FromMessage()`/`SpecVersion` -- closes #10. (Milestone 46; see `ROADMAP.md` for full detail.)

### v0.45.0 -- 2026-07-27

`rcp_zone_string()` PascalCase + `rcp_zone_from_string()` -- closes #11. (Milestone 45; see `ROADMAP.md` for full detail.)

### v0.44.0 -- 2026-07-27

Wire decoder integer-overflow fix -- closes #9. (Milestone 44; see `ROADMAP.md` for full detail.)

### v0.43.0 -- 2026-07-27

Certification. (Milestone 43; see `ROADMAP.md` for full detail.)

### v0.42.0 -- 2026-07-27

ISO 21434 / Cybersecurity. (Milestone 42; see `ROADMAP.md` for full detail.)

### v0.41.0 -- 2026-07-27

Formal Verification. (Milestone 41; see `ROADMAP.md` for full detail.)

### v0.40.0 -- 2026-07-27

RTOS / Bare-Metal. (Milestone 40; see `ROADMAP.md` for full detail.)

### v0.39.0 -- 2026-07-27

DoIP Bridge. (Milestone 39; see `ROADMAP.md` for full detail.)

### v0.38.0 -- 2026-07-27

UDS Bridge. (Milestone 38; see `ROADMAP.md` for full detail.)

### v0.37.0 -- 2026-07-27

LIN Bridge. (Milestone 37; see `ROADMAP.md` for full detail.)

### v0.36.0 -- 2026-07-27

MQTT Bridge. (Milestone 36; see `ROADMAP.md` for full detail.)

### v0.35.0 -- 2026-07-27

DDS Bridge. (Milestone 35; see `ROADMAP.md` for full detail.)

### v0.34.0 -- 2026-07-27

CAN Bridge. (Milestone 34; see `ROADMAP.md` for full detail.)

### v0.33.0 -- 2026-07-27

SOME/IP Bridge. (Milestone 33; see `ROADMAP.md` for full detail.)

### v0.32.0 -- 2026-07-27

REST Bridge. (Milestone 32; see `ROADMAP.md` for full detail.)

### v0.31.0 -- 2026-07-27

gRPC Bridge. (Milestone 31; see `ROADMAP.md` for full detail.)

### v0.30.0 -- 2026-07-27

Dynamic Data. (Milestone 30; see `ROADMAP.md` for full detail.)

### v0.29.0 -- 2026-07-27

Code Generation. (Milestone 29; see `ROADMAP.md` for full detail.)

### v0.28.0 -- 2026-07-27

Config. (Milestone 28; see `ROADMAP.md` for full detail.)

### v0.27.0 -- 2026-07-27

Record & Replay. (Milestone 27; see `ROADMAP.md` for full detail.)

### v0.26.0 -- 2026-07-27

Admin API. (Milestone 26; see `ROADMAP.md` for full detail.)

### v0.25.0 -- 2026-07-27

Observability. (Milestone 25; see `ROADMAP.md` for full detail.)

### v0.24.0 -- 2026-07-27

Multi-HPC Federation. (Milestone 24; see `ROADMAP.md` for full detail.)

### v0.23.0 -- 2026-07-27

Redundancy. (Milestone 23; see `ROADMAP.md` for full detail.)

### v0.22.0 -- 2026-07-27

Zone Proxy. (Milestone 22; see `ROADMAP.md` for full detail.)

### v0.21.0 -- 2026-07-27

Zone Groups. (Milestone 21; see `ROADMAP.md` for full detail.)

### v0.20.0 -- 2026-07-27

Firmware Update / OTA. (Milestone 20; see `ROADMAP.md` for full detail.)

### v0.19.0 -- 2026-07-27

Authorization. (Milestone 19; see `ROADMAP.md` for full detail.)

### v0.18.0 -- 2026-07-27

Fault Injection. (Milestone 18; see `ROADMAP.md` for full detail.)

### v0.17.0 -- 2026-07-27

Zone Simulator. (Milestone 17; see `ROADMAP.md` for full detail.)

### v0.16.0 -- 2026-07-27

Rate Limiting. (Milestone 16; see `ROADMAP.md` for full detail.)

### v0.15.0 -- 2026-07-27

Priority Queuing. (Milestone 15; see `ROADMAP.md` for full detail.)

### v0.14.0 -- 2026-07-27

E2E Protection. (Milestone 14; see `ROADMAP.md` for full detail.)

### v0.13.0 -- 2026-07-27

Power State. (Milestone 13; see `ROADMAP.md` for full detail.)

### v0.12.0 -- 2026-07-27

Deadline Monitoring. (Milestone 12; see `ROADMAP.md` for full detail.)

### v0.11.0 -- 2026-07-27

Watchdog & Heartbeat. (Milestone 11; see `ROADMAP.md` for full detail.)

### v0.10.0 -- 2026-07-27

TSN Transport. (Milestone 10; see `ROADMAP.md` for full detail.)

### v0.9.0 -- 2026-07-27

Loaned Samples. (Milestone 9; see `ROADMAP.md` for full detail.)

### v0.8.0 -- 2026-07-27

Shared Memory Transport. (Milestone 8; see `ROADMAP.md` for full detail.)

### v0.7.0 -- 2026-07-27

TLS Transport. (Milestone 7; see `ROADMAP.md` for full detail.)

### v0.6.0 -- 2026-07-27

mDNS Discovery. (Milestone 6; see `ROADMAP.md` for full detail.)

### v0.5.0 -- 2026-07-27

UDP Transport. (Milestone 5; see `ROADMAP.md` for full detail.)

### v0.4.0 -- 2026-07-27

HARA Expansion. (Milestone 4; see `ROADMAP.md` for full detail.)

### v0.3.0 -- 2026-07-27

Hardening. (Milestone 3; see `ROADMAP.md` for full detail.)

### v0.2.0 -- 2026-07-27

Requirements. (Milestone 2; see `ROADMAP.md` for full detail.)

### v0.1.0 -- 2026-07-27

Foundation. (Milestone 1; see `ROADMAP.md` for full detail.)

