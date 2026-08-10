/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/regmap.h"

#include <string.h>

/* ── EP0 ────────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-001
bool rcp_regmap_is_ep0(uint16_t ep_index)
{
    return ep_index == RCP_REGMAP_EP0_INDEX;
}

/* ── svr_implemented_options: REQ-RMAP-004..008 retired (REQ-RMAP-030) ────── */

//cfusa:req REQ-RMAP-004
//cfusa:req REQ-RMAP-005
//cfusa:req REQ-RMAP-006
//cfusa:req REQ-RMAP-007
//cfusa:req REQ-RMAP-008
/* REQ-RMAP-004..008 (formerly: "svr_implemented_options bit assignments
 * are pairwise distinct" / rcp_regmap_options_group_consistent()'s own
 * four accept/reject behaviors) are RETIRED as of this milestone --
 * see .fusa-reqs.json for each entry's own full retirement text. Direct
 * primary-source verification (OA_TC18_specification_v_0.5.1_RC.pdf,
 * §12.9.1.1, page 64) found their shared citation incorrect: that
 * section is entirely about an RC Server handling multiple ACF-type
 * requests packed into one AVTPDU frame -- it says nothing about
 * svr_implemented_options, feature advertisement, or any bit-pairing
 * rule. The all-or-nothing-pair grouping these five requirements
 * described, and the rcp_regmap_options_group_consistent() function
 * that enforced it, had no TC18 basis and are removed outright (not
 * deprecated-then-removed; there is no correct behavior to preserve a
 * transition window for). REQ-RMAP-030's own field comment (regmap.h)
 * gives the correct, primary-source-verified replacement: five
 * independent single bits, matching Table 18 exactly. */

/* ── The general register map ──────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-003
//cfusa:req REQ-RMAP-023
void rcp_regmap_general_init(rcp_regmap_general_t *map)
{
    memset(map, 0, sizeof(*map));
    map->svr_root_client_index = RCP_REGMAP_NO_ROOT_CLIENT;
    /* svr_lifecycle_state == 0 == RCP_LIFECYCLE_HW_UNCONFIGURED, the
     * correct default -- matches every real server's own starting
     * rcp_lifecycle_state_t (see server.h/mock.h). */
}

/* ── Root-client / per-EP-restricted-client model ──────────────────────────── */

//cfusa:req REQ-RMAP-009
//cfusa:req REQ-RMAP-010
//cfusa:req REQ-RMAP-011
//cfusa:req REQ-RMAP-012
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0,
                                               bool via_unicast)
{
    rcp_lifecycle_writer_ctx_t ctx;

    ctx.via_root_client_ep0 = via_ep0 &&
                              map->svr_root_client_index != RCP_REGMAP_NO_ROOT_CLIENT &&
                              requesting_stream_index == map->svr_root_client_index;

    ctx.via_owning_stream = ep_client != NULL &&
                            ep_client->has_owning_stream &&
                            requesting_stream_index == ep_client->owning_stream_index;

    ctx.via_non_unicast_frame = !via_unicast;

    return ctx;
}

/* ── The generic-vs-functional per-endpoint config split ───────────────────── */

//cfusa:req REQ-RMAP-016
void rcp_regmap_ep_generic_cfg_init(rcp_regmap_ep_generic_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-017
void rcp_regmap_ep_functional_cfg_init(rcp_regmap_ep_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

/* ── Per-endpoint-type named-signal index ──────────────────────────────────── */

//cfusa:req REQ-RMAP-014
//cfusa:req REQ-RMAP-015
const char *rcp_regmap_named_signal_string(rcp_regmap_named_signal_t sig)
{
    switch (sig) {
    case RCP_REGMAP_SIGNAL_GPIO0:  return "GPIO0";
    case RCP_REGMAP_SIGNAL_GPIO1:  return "GPIO1";
    case RCP_REGMAP_SIGNAL_GPIO2:  return "GPIO2";
    case RCP_REGMAP_SIGNAL_GPIO3:  return "GPIO3";
    case RCP_REGMAP_SIGNAL_GPIO4:  return "GPIO4";
    case RCP_REGMAP_SIGNAL_GPIO5:  return "GPIO5";
    case RCP_REGMAP_SIGNAL_GPIO6:  return "GPIO6";
    case RCP_REGMAP_SIGNAL_GPIO7:  return "GPIO7";
    case RCP_REGMAP_SIGNAL_GPIO8:  return "GPIO8";
    case RCP_REGMAP_SIGNAL_GPIO9:  return "GPIO9";
    case RCP_REGMAP_SIGNAL_GPIO10: return "GPIO10";
    case RCP_REGMAP_SIGNAL_GPIO11: return "GPIO11";
    case RCP_REGMAP_SIGNAL_GPIO12: return "GPIO12";
    case RCP_REGMAP_SIGNAL_GPIO13: return "GPIO13";
    case RCP_REGMAP_SIGNAL_GPIO14: return "GPIO14";
    case RCP_REGMAP_SIGNAL_GPIO15: return "GPIO15";
    case RCP_REGMAP_SIGNAL_GPIO16: return "GPIO16";
    case RCP_REGMAP_SIGNAL_GPIO17: return "GPIO17";
    case RCP_REGMAP_SIGNAL_GPIO18: return "GPIO18";
    case RCP_REGMAP_SIGNAL_GPIO19: return "GPIO19";
    case RCP_REGMAP_SIGNAL_GPIO20: return "GPIO20";
    case RCP_REGMAP_SIGNAL_GPIO21: return "GPIO21";
    case RCP_REGMAP_SIGNAL_GPIO22: return "GPIO22";
    case RCP_REGMAP_SIGNAL_GPIO23: return "GPIO23";
    case RCP_REGMAP_SIGNAL_GPIO24: return "GPIO24";
    case RCP_REGMAP_SIGNAL_GPIO25: return "GPIO25";
    case RCP_REGMAP_SIGNAL_GPIO26: return "GPIO26";
    case RCP_REGMAP_SIGNAL_GPIO27: return "GPIO27";
    case RCP_REGMAP_SIGNAL_GPIO28: return "GPIO28";
    case RCP_REGMAP_SIGNAL_GPIO29: return "GPIO29";
    case RCP_REGMAP_SIGNAL_GPIO30: return "GPIO30";
    case RCP_REGMAP_SIGNAL_GPIO31: return "GPIO31";
    case RCP_REGMAP_SIGNAL_SPI_CLK:  return "SPI_CLK";
    case RCP_REGMAP_SIGNAL_SPI_PICO: return "SPI_PICO";
    case RCP_REGMAP_SIGNAL_SPI_POCI: return "SPI_POCI";
    case RCP_REGMAP_SIGNAL_SPI_CS0:  return "SPI_CS0";
    case RCP_REGMAP_SIGNAL_SPI_CS1:  return "SPI_CS1";
    case RCP_REGMAP_SIGNAL_SPI_CS2:  return "SPI_CS2";
    case RCP_REGMAP_SIGNAL_SPI_CS3:  return "SPI_CS3";
    case RCP_REGMAP_SIGNAL_SPI_CS4:  return "SPI_CS4";
    case RCP_REGMAP_SIGNAL_SPI_CS5:  return "SPI_CS5";
    case RCP_REGMAP_SIGNAL_I2C_SCL:  return "I2C_SCL";
    case RCP_REGMAP_SIGNAL_I2C_SDA:  return "I2C_SDA";
    case RCP_REGMAP_SIGNAL_UART_TX:  return "UART_TX";
    case RCP_REGMAP_SIGNAL_UART_RX:  return "UART_RX";
    case RCP_REGMAP_SIGNAL_UART_RTS: return "UART_RTS";
    case RCP_REGMAP_SIGNAL_UART_CTS: return "UART_CTS";
    case RCP_REGMAP_SIGNAL_LIN_TXD:  return "LIN_TXD";
    case RCP_REGMAP_SIGNAL_LIN_RXD:  return "LIN_RXD";
    case RCP_REGMAP_SIGNAL_LIN_NSLP: return "LIN_NSLP";
    case RCP_REGMAP_SIGNAL_PWM_OUT:  return "PWM_OUT";
    case RCP_REGMAP_SIGNAL_PWM_OUTN: return "PWM_OUTN";
    case RCP_REGMAP_SIGNAL_PWM_IN:   return "PWM_IN";
    case RCP_REGMAP_SIGNAL_ADC_IN:   return "ADC_IN";
    case RCP_REGMAP_SIGNAL_DAC_OUT:  return "DAC_OUT";
    case RCP_REGMAP_SIGNAL_CAN_RXD:  return "CAN_RXD";
    case RCP_REGMAP_SIGNAL_CAN_TXD:  return "CAN_TXD";
    case RCP_REGMAP_SIGNAL_ISELED_ISP_P: return "ISELED_ISP_P";
    case RCP_REGMAP_SIGNAL_ISELED_ISP_N: return "ISELED_ISP_N";
    case RCP_REGMAP_SIGNAL_MDIO_MDC:  return "MDIO_MDC";
    case RCP_REGMAP_SIGNAL_MDIO_DATA: return "MDIO_DATA";
    default: return "unknown";
    }
}

//cfusa:req REQ-RMAP-045
uint8_t rcp_regmap_named_signal_ep_signal_nr(rcp_regmap_named_signal_t sig)
{
    /* Explicit lower bound: RCP_REGMAP_SIGNAL_GPIO0 == 0, so the range
     * checks below (each an upper-bound-only "sig <= ...") would
     * otherwise treat a negative/garbage sig as if it were a small
     * GPIO index, silently violating this function's own "0 for any
     * out-of-range value" documented contract. */
    if (sig < RCP_REGMAP_SIGNAL_GPIO0) return 0u;

    if (sig <= RCP_REGMAP_SIGNAL_GPIO31) {
        return (uint8_t)sig; /* GPIOn's own per-type number is n */
    }
    if (sig <= RCP_REGMAP_SIGNAL_SPI_CS5) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_SPI_CLK);
    }
    if (sig <= RCP_REGMAP_SIGNAL_I2C_SDA) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_I2C_SCL);
    }
    if (sig <= RCP_REGMAP_SIGNAL_UART_CTS) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_UART_TX);
    }
    if (sig <= RCP_REGMAP_SIGNAL_LIN_NSLP) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_LIN_TXD);
    }
    if (sig <= RCP_REGMAP_SIGNAL_PWM_OUTN) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_PWM_OUT);
    }
    if (sig == RCP_REGMAP_SIGNAL_PWM_IN)  return 0u;
    if (sig == RCP_REGMAP_SIGNAL_ADC_IN)  return 0u;
    if (sig == RCP_REGMAP_SIGNAL_DAC_OUT) return 0u;
    if (sig <= RCP_REGMAP_SIGNAL_CAN_TXD) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_CAN_RXD);
    }
    if (sig <= RCP_REGMAP_SIGNAL_ISELED_ISP_N) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_ISELED_ISP_P);
    }
    if (sig <= RCP_REGMAP_SIGNAL_MDIO_DATA) {
        return (uint8_t)(sig - RCP_REGMAP_SIGNAL_MDIO_MDC);
    }
    return 0u; /* RCP_REGMAP_SIGNAL_COUNT or any other invalid value */
}

/* ── Request-stream and response/ack queue config ──────────────────────────── */

//cfusa:req REQ-RMAP-018
void rcp_regmap_request_stream_cfg_init(rcp_regmap_request_stream_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-019
void rcp_regmap_response_queue_cfg_init(rcp_regmap_response_queue_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

//cfusa:req REQ-RMAP-060
rcp_stream_id_t rcp_regmap_response_queue_stream_id(const rcp_regmap_response_queue_cfg_t *cfg,
                                                     const uint8_t mac[6])
{
    return rcp_stream_id_make(mac, cfg->stream_uid);
}

/* ── EP-ID / byte_bus_id map ────────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-020
//cfusa:req REQ-RMAP-021
//cfusa:req REQ-RMAP-022
bool rcp_regmap_ep_id_map_is_ascending(const rcp_regmap_ep_id_map_entry_t *entries,
                                        size_t count)
{
    size_t i;

    if (count < 2) return true; /* vacuously ascending */

    for (i = 1; i < count; i++) {
        const rcp_regmap_ep_id_map_entry_t *prev = &entries[i - 1];
        const rcp_regmap_ep_id_map_entry_t *cur   = &entries[i];

        if (prev->request_stream_index != cur->request_stream_index) {
            /* A higher stream index always counts as ascending -- TC18
             * does not require byte_bus_id to relate across different
             * streams, only strictly increase within one (REQ-RMAP-056). */
            if (!(prev->request_stream_index < cur->request_stream_index)) return false;
            continue;
        }
        if (!(prev->byte_bus_id < cur->byte_bus_id)) return false;
    }

    return true;
}

//cfusa:req REQ-RMAP-054
size_t rcp_regmap_ep_id_map_effective_count(const rcp_regmap_ep_id_map_entry_t *entries,
                                             size_t capacity)
{
    size_t i;

    for (i = 0; i < capacity; i++) {
        if (entries[i].request_stream_index == 0u) return i;
    }

    return capacity;
}

//cfusa:req REQ-RMAP-054
void rcp_regmap_ep_id_map_row_init_default(rcp_regmap_ep_id_map_entry_t *row)
{
    row->request_stream_index = 1u;
    row->ep_id                = RCP_REGMAP_EP0_INDEX;
    row->byte_bus_id          = 0u;
}

//cfusa:req REQ-RMAP-057
bool rcp_regmap_ep_id_map_has_single_client_per_ep(const rcp_regmap_ep_id_map_entry_t *entries,
                                                    size_t count)
{
    size_t i, j;

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (entries[i].ep_id == entries[j].ep_id &&
                entries[i].request_stream_index != entries[j].request_stream_index) {
                return false;
            }
        }
    }

    return true;
}

//cfusa:req REQ-RMAP-058
bool rcp_regmap_ep_id_map_shared_bus_homogeneous(const rcp_regmap_ep_id_map_entry_t *entries,
                                                  const uint8_t *ep_types,
                                                  size_t count)
{
    size_t i, j;

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (entries[i].request_stream_index == entries[j].request_stream_index &&
                entries[i].byte_bus_id == entries[j].byte_bus_id &&
                ep_types[i] != ep_types[j]) {
                return false;
            }
        }
    }

    return true;
}
