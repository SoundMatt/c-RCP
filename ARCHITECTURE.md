# Architecture

c-RCP's architecture follows the canonical cross-repo design at
[RELAY's `docs/RCP-ARCHITECTURE.md`](https://github.com/SoundMatt/RELAY/blob/main/docs/RCP-ARCHITECTURE.md),
shared with go-RCP, cpp-RCP, and rust-RCP.

## File-path mapping

| Lexicon term | This repo |
|---|---|
| wire / ACF layer | `src/acf.c` / `include/rcp/acf.h` |
| framing / AVTP layer | `src/avtp.c` / `include/rcp/avtp.h` |
| response classification | `rcp_acf_classify_response()` in `src/acf.c` |
| conditional-request layer | `src/request_compound.c`, `src/request_chained.c`, `src/request_triggered.c`, `src/request_timed.c`, `src/request_cancel.c` |
| Table 30 / evt[2:0] write semantics | *(not centralized — see below)* |
| endpoint-type modules | `src/ep_gpio.c`, `src/ep_spi.c`, `src/ep_pwm.c`, `src/ep_adc.c`, `src/ep_i2c.c`, `src/ep_lin.c`, `src/ep_can.c`, `src/ep_uart.c`, `src/ep_iseled.c`, `src/ep_mdio.c`, `src/ep_wakeup.c` |
| dispatch/routing | `src/server.c` / `src/mock.c` |

## Conformance status against the canonical architecture

| Canonical choice | Status |
|---|---|
| Response classification (evt-first) | conformant (fixed, c-RCP#151) |
| Table 30 centralization | **not conformant** — reimplemented per endpoint (`ep_can.h`/`ep_lin.h`/`ep_pwm.h`/`ep_spi.h`/`ep_gpio.h`); tracked |
| Conditional-request module unification | **not conformant** — five separate files; target is one unified module per cpp-RCP/rust-RCP's shape |
| Per-function requirement tagging | **conformant** (this repo is the reference for this convention) |
| `.fusa-reqs.json` schema (`tc18`/`tc18_master_id`/`status`) | **partial** — `tc18`/`scope`/`status` already exist (this repo is the reference for that part of the schema); `tc18_master_id` propagation in progress |
| Conditional-request req-id grouping | **conformant** (this repo is the reference: `REQ-CMP-*`/`REQ-TRIG-*`/`REQ-CHAIN-*`/`REQ-TIMED-*`/`REQ-CANCEL-*`) |
