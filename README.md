# c-RCP

A pure-C99 implementation of the OPEN Alliance TC18 Remote Control
Protocol (RCP) — the automotive standard this project is named after. It
lets a central/zonal ECU's application logic drive low-level peripheral
interfaces (SPI, GPIO, I²C, UART, ADC, PWM, LIN, CAN, ISELED, MDIO, ...)
physically wired to a separate, simpler ECU — the RC Server — over an
IEEE 1722-framed Ethernet (or CAN(FD/XL)) link, without that simpler ECU
needing any OEM-specific application logic of its own.

*(Historical note: through v0.58.0, this project instead implemented an
informal, bespoke Zone/Command/Response/Status protocol and described
itself as "a feature and API mirror of cpp-RCP." A full gap analysis
found that protocol shared nothing at the wire level with the real TC18
standard, and Phases 13–22 replaced it outright — see `ROADMAP.md`'s
Protocol Replacement Notice. This project no longer mirrors cpp-RCP/
go-RCP/rust-RCP port-for-port; requirements are derived directly from
the OPEN Alliance TC18 Remote Control Protocol Specification v0.5.1_RC.
The pre-replacement Zone/Command surface that used to live in
`include/rcp/rcp.h`/`src/rcp.c` (retained for a time for pre-v0.59
consumers) has since been removed outright with no compatibility shim,
per RELAY spec §15.5 — see "Removed legacy API" below.)*

*(Wire-interop status: through v0.99.0, `acf.h`/`acf.c`'s
`byte_message_info` header was this implementation's own invented byte
layout, not the specification's — self-documented as such in `acf.h`'s
own file header at the time, but never called out here. v0.100.0
replaced it with the specification's real Figure 7 / Table 4 bit layout
and quadlet-counted `acf_msg_length`, verified against the
specification's own Figure 19/Figure 20 worked examples (see
`CHANGELOG.md`). This is a **breaking wire-format change** from every
prior tagged release. As of v0.100.0 the ACF header itself is
byte-for-byte conformant, but this remains one of four independent RCP
implementations under active cross-repo reconciliation (`go-RCP`,
`cpp-RCP`, `rust-RCP`); it has not yet been live-tested against a real
third-party TC18 peer, and known gaps remain — most notably, no
endpoint module yet populates the specification's numbered wire error
codes (`errors.h`) on an actual Error response, and a worst-case CAN XL
new-payload write *request* (`ep_can.h`) cannot yet be sent at all
under the corrected 9-bit `acf_msg_length` field, having no fragmented
request path. Treat "TC18 implementation" below as "TC18 wire-header-
conformant, gaps tracked in `ROADMAP.md`," not "drop-in interop with an
arbitrary TC18 peer.")*

[![CI](https://github.com/SoundMatt/c-RCP/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/c-RCP/actions/workflows/ci.yml)
[![DCO](https://github.com/SoundMatt/c-RCP/actions/workflows/dco.yml/badge.svg)](https://github.com/SoundMatt/c-RCP/actions/workflows/dco.yml)

## Headers

| Header | Description |
|---|---|
| `<rcp/avtp.h>` | IEEE 1722 AVTPDU framing, `(stream_id, byte_bus_id)` addressing |
| `<rcp/acf.h>` | ACF message encode/decode (`ACF_ABB`/`ACF_GBB`) |
| `<rcp/regmap.h>` | Register-map model: general/endpoint/stream config, writer authorization |
| `<rcp/lifecycle.h>` | RC Server lifecycle state machine (`HW_UNCONFIGURED`/`HW_CONFIGURED`/`RCP_CONFIGURED`) |
| `<rcp/discovery.h>` | Native broadcast discovery and bootstrap claim |
| `<rcp/e2e.h>` | CRC32 safe points, safety-tagged request execution gating, per-stream watchdog |
| `<rcp/fragment.h>` | Multi-segment request/response fragmentation |
| `<rcp/power.h>` | Normal/StandBy/Sleep/Unpowered power-mode model and WakeUp handshake |
| `<rcp/ep_gpio.h>`, `<rcp/ep_spi.h>`, `<rcp/ep_i2c.h>`, `<rcp/ep_uart.h>`, `<rcp/ep_adc.h>`, `<rcp/ep_pwm.h>`, `<rcp/ep_can.h>`, `<rcp/ep_lin.h>`, `<rcp/ep_mdio.h>`, `<rcp/ep_iseled.h>`, `<rcp/ep_wakeup.h>` | Per-endpoint-type request/response codecs |
| `<rcp/request.h>`, `<rcp/request_sequencer.h>` | The request-kind taxonomy (compound, compound-wait, triggered, chained, timed, cancellation; execution priority: cancellation > triggered > timed > compound > compound-wait > chained > standard) |
| `<rcp/ingress.h>` | Pure receive-path stages: AVTPDU parse, ACF member iterator, frame timing decision, Table 4 fixed-field check |
| `<rcp/mock.h>` | In-process reference RC Server — zero I/O; `rcp_mock_server_receive_avtpdu()` is the receive pipeline |
| `<rcp/errors.h>` | Numbered TC18 wire error codes (`rcp_wire_error_t`) |
| `<relay/relay.h>` | Shared `rcp_context_t` (deadline) and error-condition types |

## Build

Requires CMake 3.16+ and a C99 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### RELAY-conformant CLI

c-RCP ships as a library by default. Pass `-DRELAY_BUILD_CLI=ON` to also
build the `c-rcp` binary, which implements the RELAY spec's mandatory
`version`/`capabilities`/`status` commands (spec §11, §17):

```bash
cmake -B build -DRELAY_BUILD_CLI=ON
cmake --build build --target c-rcp
./build/c-rcp version
./build/c-rcp capabilities
```

## CLI capabilities self-report

`c-rcp capabilities`'s `features` array (`time_sync`, `enhanced_cancel`,
`compound_bundles`) reports **API-surface presence, not full TC18 wire
conformance**. Each has real, working code behind it, but none is fully
spec-conformant yet as of this writing: `time_sync`'s presentation-time
sub-field (`request_timed.c`) is narrower than the spec's own field;
`enhanced_cancel`'s encoders (`request_cancel.c`) hard-code the shared
ACF header's `evt` sub-field to 0, so half the mechanism can't be
expressed on the wire; `compound_bundles`' repetition count
(`request_compound.c`) is round-tripped only, with no repetition state
machine actually driving repeated execution. The RELAY capabilities
JSON schema's `features` array is a flat list of strings with no room
for a per-entry conformance caveat, so this note — and `src/cli.c`'s own
comment above `capabilities_json()` — is where that caveat actually
lives. Treat a `features` entry as "this build compiles the named
group's request/response codec", not as a spec-conformance claim.

## Quick start

The example below drives the in-process reference RC Server
(`<rcp/mock.h>`) through its lifecycle, registers one endpoint, and hands
it one raw NTSCF AVTPDU through `rcp_mock_server_receive_avtpdu()` -- the
single receive pipeline (parse, frame timing, lifecycle filtering,
sequence gate, then per ACF member: stream-fault gate, Table 4 fixed
fields, endpoint lookup, reassembly, safe-command-mode CRC, admission).
A real I/O loop does the same with the bytes its transport receives
(`<rcp/udp.h>` for IEEE1722-over-UDP/IP, `<rcp/l2.h>` for native
Ethernet), or calls `rcp_mock_server_pump_transport()` to do the
`recv()` too. The older `rcp_mock_server_dispatch*()` entry points are
deprecated wrappers over the same pipeline.

```c
#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <assert.h>
#include <string.h>

static void echo_handler(const uint8_t *req, size_t req_len,
                         rcp_bytes_t *out_response, void *user_data) {
    (void)user_data;
    /* A real handler decodes req with the endpoint type's ep_*.h codec
     * and encodes its answer with that module's _encode_response();
     * leaving *out_response zeroed means "no response frame". */
    *out_response = rcp_bytes_dup(req, req_len);
}

/* Receives each member's outcome; may move responses out (zero them). */
static void on_member(rcp_mock_rx_member_t *m, void *user) {
    *(rcp_mock_dispatch_result_t *)user = m->result;
}

int main(void) {
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_lifecycle_plausibility_snapshot_t snap;
    rcp_lifecycle_writer_ctx_t none = {0}, root = {0};
    rcp_acf_byte_message_info_t hdr;
    rcp_avtp_ntscf_header_t ntscf;
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    rcp_bytes_t request, avtpdu;
    rcp_mock_rx_ctx_t rx = {true /* time sync */, 0 /* gPTP now */, false};
    rcp_mock_rx_summary_t summary;
    rcp_mock_dispatch_result_t result = RCP_MOCK_DISPATCH_DROPPED;

    memset(&snap, 0, sizeof(snap));
    root.via_root_client_ep0 = true;
    rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none, true);
    rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root, true);
    rcp_mock_server_add_endpoint(srv, /*byte_bus_id=*/1, /*ep_type=*/0,
                                 /*ep_enable=*/true, echo_handler, NULL);

    /* One ACF_ABB request to endpoint 1, inside a real NTSCF AVTPDU --
     * exactly the bytes a transport hands over. */
    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 1;
    hdr.op = RCP_ACF_OP_WRITE;
    request = rcp_acf_encode_abb(&hdr, (const uint8_t *)"\x01\x02\x03", 3);
    memset(&ntscf, 0, sizeof(ntscf));
    ntscf.sv = 1;
    ntscf.stream_id = rcp_stream_id_make(mac, 1);
    avtpdu = rcp_avtp_encode_ntscf(&ntscf, request.data, request.len);

    rcp_mock_server_receive_avtpdu(srv, avtpdu.data, avtpdu.len, &rx,
                                   on_member, &result, &summary);
    assert(summary.outcome == RCP_MOCK_RX_FRAME_ACCEPTED);
    assert(result == RCP_MOCK_DISPATCH_OK);

    rcp_bytes_free(&avtpdu);
    rcp_bytes_free(&request);
    rcp_mock_server_destroy(srv);
    return 0;
}
```

## Lifecycle states

| Constant | Description |
|---|---|
| `RCP_LIFECYCLE_HW_UNCONFIGURED` | Initial state; only the discovery request is admitted |
| `RCP_LIFECYCLE_HW_CONFIGURED` | Hardware pin map and request-stream configuration validated |
| `RCP_LIFECYCLE_RCP_CONFIGURED` | Endpoint/stream associations validated; `FUNCTIONAL_W_STAR` register fields now permanently locked for this session |

## Error codes

Errors are returned as `rcp_errc_t` values (a plain `int` return code; `RCP_OK` is 0/success).

| Sentinel | Description |
|---|---|
| `RCP_ERR_CLOSED` | Controller, registry, or transport is closed |
| `RCP_ERR_NOT_FOUND` | Address not found / not registered |
| `RCP_ERR_ALREADY_EXISTS` | Already registered |
| `RCP_ERR_TIMEOUT` | Operation timed out or context expired |
| `RCP_ERR_BUSY` | Resource busy (rate limit hit, queue full) |
| `RCP_ERR_NOT_SUPPORTED` | Operation not implemented by this vtable/backend |
| `RCP_ERR_FORBIDDEN` | Rejected by an authorization policy |

Most module-specific errors (e.g. `rcp_lifecycle_errc_t`,
`rcp_e2e_errc_t`, `rcp_mock_errc_t`, `rcp_discovery_errc_t`) are their
own small enums with a matching `rcp_*_strerror()`, not folded into
`rcp_errc_t` — see each header.

## Safety and security

c-RCP targets deployment in automotive safety-critical environments.

- Safety standard: ISO 26262 ASIL-B baseline (see `HARA.md` — several
  hazards currently compute to ASIL-C/D, open and tracked)
- Security standard: IEC 62443 SL-2 / ISO 21434 (see `CYBERSECURITY.md`,
  `tara.md`) — MACsec (802.1AE) is the spec's own link-layer security
  control and is a deployment-level dependency, not implemented within
  this library
- Formally verified lifecycle and E2E safe-point mechanisms (`tla/`, see
  `FORMAL_VERIFICATION.md`)
- c-FuSa static analysis (MISRA-C:2012 / CERT-C) runs in CI on every PR

## Removed legacy API

`include/rcp/rcp.h` and `src/rcp.c` used to also define the pre-TC18
`rcp_zone_t`/`rcp_command_t`/`rcp_response_t`/`rcp_status_t`/
`rcp_controller_t`/`rcp_registry_t` surface (plus the `'RC'`-magic
`wire.h`/`wire.c` codec and the `sim.h`/`sim.c` zone-controller
simulator built on top of it), retained for a time for existing
pre-v0.59 consumers. It was never part of this library's TC18
conformance claim or its ISO 26262/ISO 21434 safety and security case
(see `HARA.md`/`tara.md`), and as of this removal no code in this
repository depended on it any more. Per RELAY spec §15.5 the removal
ships with no compatibility shim; `rcp.h`/`rcp.c` now define only the
protocol-agnostic primitives (`rcp_bytes_t`, the base `rcp_errc_t`
sentinels, `rcp_context_t`) that the TC18 modules above actually share.
Anyone needing the full pre-replacement protocol as it stood before
Phase 13, or the placeholder wire codec/simulator as they stood before
this removal, can pin to the `v0.90.x` tag series or earlier, which
remains a valid, buildable snapshot indefinitely.

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md) for the per-release summary,
including a dedicated deprecation/replacement/removal log. `ROADMAP.md`
remains the source of full per-milestone detail.
