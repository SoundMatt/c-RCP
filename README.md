# c-RCP

A pure-C99 library implementing the Remote Control Protocol (RCP) for zonal control in automotive systems.

RCP connects a high-performance central computer to distributed Ethernet-based zone controllers, keeping application logic centralised while remote zones provide access to local I/O, sensors, CAN/LIN gateways, and actuators.

Feature and API equivalent of [cpp-RCP](https://github.com/SoundMatt/cpp-RCP) and [go-RCP](https://github.com/SoundMatt/go-RCP), ported to pure C for targets without a C++ toolchain.

[![CI](https://github.com/SoundMatt/c-RCP/actions/workflows/ci.yml/badge.svg)](https://github.com/SoundMatt/c-RCP/actions/workflows/ci.yml)
[![DCO](https://github.com/SoundMatt/c-RCP/actions/workflows/dco.yml/badge.svg)](https://github.com/SoundMatt/c-RCP/actions/workflows/dco.yml)

## Headers

| Header | Description |
|---|---|
| `<rcp/rcp.h>` | Core interfaces: `rcp_controller_t`, `rcp_registry_t`, `rcp_command_t`, `rcp_response_t`, `rcp_status_t`, `rcp_zone_t` |
| `<rcp/mock.h>` | In-process mock controller and registry — zero I/O, default for unit tests |
| `<relay/relay.h>` | Shared `rcp_context_t` (deadline) and error-condition types |

## Build

Requires CMake 3.16+ and a C99 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Quick start

```c
#include <rcp/rcp.h>
#include <rcp/mock.h>
#include <assert.h>

int main(void) {
    rcp_registry_t *reg = rcp_mock_registry_new();

    rcp_controller_t *ctrl = NULL;
    rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &ctrl);

    rcp_command_t cmd = {0};
    cmd.id       = 1;
    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.type     = RCP_CMD_SET;
    cmd.priority = RCP_PRIORITY_NORMAL;

    rcp_response_t resp = {0};
    rcp_context_t ctx = rcp_context_background();
    int rc = rcp_controller_send(ctrl, &ctx, &cmd, &resp);
    assert(rc == RCP_OK);
    assert(resp.status == RCP_RESPONSE_OK);
    rcp_response_free(&resp);

    rcp_controller_release(ctrl);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
    return 0;
}
```

## Zones

| Constant | Value | Description |
|---|---|---|
| `RCP_ZONE_UNKNOWN` | 0 | Zero value / uninitialized |
| `RCP_ZONE_FRONT_LEFT` | 1 | Front-left zone controller |
| `RCP_ZONE_FRONT_RIGHT` | 2 | Front-right zone controller |
| `RCP_ZONE_REAR_LEFT` | 3 | Rear-left zone controller |
| `RCP_ZONE_REAR_RIGHT` | 4 | Rear-right zone controller |
| `RCP_ZONE_CENTRAL` | 5 | Central zone controller |

## Command types

| Constant | Value | Description |
|---|---|---|
| `RCP_CMD_NOOP` | 0 | No-op / keepalive |
| `RCP_CMD_SET` | 1 | Set an output or actuator state |
| `RCP_CMD_GET` | 2 | Query current state |
| `RCP_CMD_RESET` | 3 | Reset zone controller |
| `RCP_CMD_WATCHDOG` | 4 | Watchdog kick |
| `RCP_CMD_SLEEP` | 5 | Request zone controller to enter low-power sleep |
| `RCP_CMD_WAKE` | 6 | Request zone controller to exit sleep |

## Error codes

Errors are returned as `rcp_errc_t` values (a plain `int` return code; `RCP_OK` is 0/success).

| Sentinel | Description |
|---|---|
| `RCP_ERR_CLOSED` | Controller or registry is closed |
| `RCP_ERR_NOT_FOUND` | Zone not found in registry |
| `RCP_ERR_ALREADY_EXISTS` | Zone already registered |
| `RCP_ERR_TIMEOUT` | Command timed out or context expired |
| `RCP_ERR_BUSY` | Zone controller busy (rate limit hit) |
| `RCP_ERR_ZONE_MISMATCH` | Command addressed to wrong zone |

## Safety

c-RCP targets deployment in automotive safety-critical environments.

- Safety standard: ISO 26262 ASIL-B baseline / IEC 61508 SIL-2 (see `HARA.md` — several hazards currently compute to ASIL-C/D, open and tracked)
- Security standard: IEC 62443 SL-2
- c-FuSa static analysis (MISRA-C:2012 / CERT-C) runs in CI on every PR
