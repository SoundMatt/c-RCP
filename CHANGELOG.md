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

## Releases

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

