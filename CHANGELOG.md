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

## Releases

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
left for its own change.

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

