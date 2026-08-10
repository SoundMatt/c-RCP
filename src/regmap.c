/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/regmap.h"

#include <string.h>

/* ── EP0 ────────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-001
bool rcp_regmap_is_ep0(uint16_t ep_index)
{
    return ep_index == RCP_REGMAP_EP0_INDEX;
}

/* ── svr_implemented_options: three all-or-nothing feature groups ─────────── */

//cfusa:req REQ-RMAP-005
//cfusa:req REQ-RMAP-006
//cfusa:req REQ-RMAP-007
//cfusa:req REQ-RMAP-008
bool rcp_regmap_options_group_consistent(uint32_t options)
{
    static const uint32_t groups[3] = {
        RCP_REGMAP_OPT_TIME_SYNC_TSCF | RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION,
        RCP_REGMAP_OPT_ENH_CANCEL_REQUEST | RCP_REGMAP_OPT_ENH_CANCEL_ACK,
        RCP_REGMAP_OPT_COMPOUND_HEADER | RCP_REGMAP_OPT_COMPOUND_SEGMENT,
    };
    size_t i;

    for (i = 0; i < 3; i++) {
        uint32_t bits = options & groups[i];

        if (bits != 0 && bits != groups[i]) return false;
    }

    return true;
}

/* ── The general register map ──────────────────────────────────────────────── */

//cfusa:req REQ-RMAP-003
void rcp_regmap_general_init(rcp_regmap_general_t *map)
{
    memset(map, 0, sizeof(*map));
    map->svr_root_client_index = RCP_REGMAP_NO_ROOT_CLIENT;
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
    default: return "unknown";
    }
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
        if (!(entries[i - 1].byte_bus_id < entries[i].byte_bus_id)) return false;
    }

    return true;
}
