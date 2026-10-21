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
| Table 30 / evt[2:0] write semantics | `rcp_acf_evt_row2_is_plain()` in `src/acf.c` / `include/rcp/acf.h` |
| endpoint-type modules | `src/ep_gpio.c`, `src/ep_spi.c`, `src/ep_pwm.c`, `src/ep_adc.c`, `src/ep_i2c.c`, `src/ep_lin.c`, `src/ep_can.c`, `src/ep_uart.c`, `src/ep_iseled.c`, `src/ep_mdio.c`, `src/ep_wakeup.c` |
| receive pipeline (pure stages) | `src/ingress.c` / `include/rcp/ingress.h` |
| dispatch/routing | `src/server.c` / `src/mock.c` (`rcp_mock_server_receive_avtpdu()`) |

## Conformance status against the canonical architecture

| Canonical choice | Status |
|---|---|
| Response classification (evt-first) | conformant (fixed, c-RCP#151) |
| Table 30 centralization | **conformant** — `rcp_acf_evt_row2_is_plain()` (`acf.h`/`acf.c`), called consistently from all 8 Row-2 endpoint files (`ep_can.c`, `ep_lin.c`, `ep_adc.c`, `ep_i2c.c`, `ep_uart.c`, `ep_iseled.c`, `ep_mdio.c`, `ep_pwm.c`); SPI and GPIO/PWM_OUT have their own separate, also-centralized predicates (`rcp_ep_spi_channel_valid()`, `rcp_ep_gpio_write_semantics_valid()`) |
| Conditional-request module unification | **not conformant** — five separate files; target is one unified module per cpp-RCP/rust-RCP's shape |
| Per-function requirement tagging | **conformant** (this repo is the reference for this convention) |
| `.fusa-reqs.json` schema (`tc18`/`tc18_master_id`/`status`) | **partial** — `tc18`/`scope`/`status` already exist (this repo is the reference for that part of the schema); `tc18_master_id` propagation in progress |
| Conditional-request req-id grouping | **conformant** (this repo is the reference: `REQ-CMP-*`/`REQ-TRIG-*`/`REQ-CHAIN-*`/`REQ-TIMED-*`/`REQ-CANCEL-*`) |

## Ingress pipeline (issue #618)

Every frame the reference RC Server receives goes through one fixed
sequence of stages. The pure stages live in `src/ingress.c`; `src/mock.c`
holds only the stateful composition (`rcp_mock_server_receive_avtpdu()`,
plus `rcp_mock_server_pump_transport()` as transport glue). The ten
legacy `rcp_mock_server_dispatch*()` entry points are deprecated thin
wrappers that build a synthetic header and enter at stage (b); the
single-request ones skip stage (e), as before.

```
raw AVTPDU
  │
  ├─(a) rcp_ingress_parse_avtpdu()      subtype, version == 0, header_octet1 kept raw
  │        └─ fail → silent drop (no watchdog kick)            ── watchdog kick
  ├─(b) rcp_ingress_resolve_timing()    §13.3 rules, tv → one decision per AVTPDU
  │        └─ DROP → silent drop
  ├─(c) rcp_lifecycle_should_accept_subtype()   TSCF in HW_*, non-NTSCF in HW_UNCONFIGURED
  │        └─ DROP → silent drop
  ├─(d) member pre-pass (rcp_ingress_member_next(), no allocation)
  │        └─ malformed / empty / no member passes the filter → silent drop
  ├─(e) sequence gate (frame_seq_gate_admits(), parsed sequence_num, once)
  │        └─ reject → every member SEQ_ERROR
  └─(f) per member, identity from its own ACF header:
          rx_lifecycle_filter()       DROP (silent) / REJECT (REQUEST_REJECTED)
          rx_stream_fault_gate()      STREAM_FAULTED (POCI_FAILURE), plain endpoints too
          chain bookkeeping           CHAIN_ERROR / CHAIN_ABORTED
          rx_fixed_fields_stage()     Table 4: hs/err/rsp/rsv/cs, ABB mtv
          endpoint lookup             ERR_UNKNOWN_BUS (silent)
          rx_reassemble_stage()       pool keyed per (stream_id, byte_bus_id)
          rx_crc_stage()              CRC over the real header (fragmented: first fragment's)
          rx_admit_member()           store / ack / execute (slot_execute()) / overflow
          per-stream response routing → sink
```

Everything that has a side effect -- responses, stream-fault latches,
stream-status changes, sequence-tracker updates, safe-state broadcasts --
happens only after the lifecycle filters, so a frame or member that must
be dropped leaves no trace. Stored and scheduled runs (`tick*()`,
`drain_endpoint*()`) execute through the same `slot_execute()`, so
multi-response endpoints produce their whole output on every path.
