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
The pre-replacement Zone/Command surface still exists in
`include/rcp/rcp.h`/`src/rcp.c`, retained only for existing pre-v0.59
consumers — see "Legacy API" below.)*

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
| `<rcp/request_cancel.h>`, `<rcp/request_chained.h>`, `<rcp/request_compound.h>`, `<rcp/request_sequencer.h>`, `<rcp/request_timed.h>`, `<rcp/request_triggered.h>` | The request-kind taxonomy (execution priority: cancellation > triggered > timed > compound > compound-wait > chained > standard) |
| `<rcp/mock.h>` | In-process RC-Server/endpoint test double — zero I/O, default for unit tests |
| `<rcp/sim.h>` | SiL/HIL simulator backend |
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

## Quick start

The example below drives the in-process mock RC Server
(`<rcp/mock.h>`) through its lifecycle, registers one endpoint, and
dispatches an already-framed request — the same shape a real transport
(`<rcp/udp.h>`, native Ethernet) hands to a real RC Server.

```c
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <assert.h>

static void noop_handler(const uint8_t *req, size_t req_len,
                          rcp_bytes_t *out_response, void *user_data) {
    (void)req; (void)req_len; (void)out_response; (void)user_data;
    /* A real handler decodes req via the endpoint-type-specific ep_*.h
     * codec and encodes a result into *out_response with that same
     * module's _encode_response(); leaving it zeroed (as it already is
     * on entry) means "no response frame" -- a fire-and-forget request. */
}

int main(void) {
    rcp_mock_server_t *srv = rcp_mock_server_new();
    assert(rcp_mock_server_state(srv) == RCP_LIFECYCLE_HW_UNCONFIGURED);

    static const rcp_lifecycle_plausibility_snapshot_t EMPTY_SNAP = {NULL, 0, NULL, 0};
    rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP);
    rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP);

    rcp_mock_server_add_endpoint(srv, /*byte_bus_id=*/1, /*ep_type=*/0,
                                  /*ep_enable=*/true, noop_handler, NULL);

    const uint8_t request[] = {0x01, 0x02, 0x03};
    rcp_bytes_t response = {0};
    rcp_mock_dispatch_result_t rc = rcp_mock_server_dispatch(
        srv, /*byte_bus_id=*/1, /*avtp_subtype=*/0, /*acf_msg_type=*/0,
        /*time_sync_supported=*/false, request, sizeof(request), &response);
    assert(rc == RCP_MOCK_DISPATCH_OK);

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

## Legacy API

`include/rcp/rcp.h` and `src/rcp.c` still define the pre-TC18
`rcp_zone_t`/`rcp_command_t`/`rcp_response_t`/`rcp_status_t`/
`rcp_controller_t`/`rcp_registry_t` surface, retained only for existing
pre-v0.59 consumers — no new code in this repository depends on it as of
v0.84.0. It is not part of this library's TC18 conformance claim or its
ISO 26262/ISO 21434 safety and security case (see `HARA.md`/`tara.md`).
New integrations should use the headers listed above instead. Anyone
needing the full pre-replacement protocol as it stood before Phase 13
can pin to the `v0.58.x` tag series, which remains a valid, buildable
snapshot indefinitely.
