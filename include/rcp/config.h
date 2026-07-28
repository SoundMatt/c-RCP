/*
 * config.h -- RC-Server/endpoint manifest loader for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite Package
 * Rework", milestone 77, "Foundational test/config satellites").
 *
 * Replaces the pre-TC18 zone-manifest JSON schema ({"zones":[{"zone":...,
 * "priority":...}]}, registering one legacy mock zone controller per
 * entry) with a manifest schema describing an RC Server's own HW pin map,
 * its list of implemented endpoints (byte_bus_id/ep_type/ep_enable,
 * matching regmap.h's svr_ep_count and per-endpoint generic config), and
 * its request-stream configuration -- feeding mock.h's new
 * rcp_mock_server_t test double (milestone 77) rather than the retired
 * zone registry.
 *
 * Example manifest:
 *   {
 *     "server": {
 *       "vendor_id": 1, "device_id": 2,
 *       "svr_implemented_options": ["time_sync", "compound_bundles"]
 *     },
 *     "hw_pin_map": [
 *       {"hw_ep_nr": 0, "hw_ep_pin_nr": 3, "pin_property": ["output", "pull_up"]}
 *     ],
 *     "endpoints": [
 *       {"byte_bus_id": 1, "ep_type": 1, "ep_enable": true}
 *     ],
 *     "streams": [
 *       {"configured": true, "rx_stream_id": 1001}
 *     ]
 *   }
 *
 * Deviation from cpp-RCP, unchanged from the old config.h: cpp-RCP signals
 * malformed input by throwing config::ParseError. C has no exceptions, so
 * rcp_config_parse_json() returns RCP_CFG_ERR_PARSE and writes a
 * description into a caller-supplied buffer instead -- the parser itself
 * is otherwise the same hand-rolled, minimal, non-general-purpose JSON
 * scanner cpp-RCP documents (it only understands this module's own
 * manifest schema, not arbitrary JSON), inherited unchanged from the old
 * config.c: bare `{...}` object regions are found by looking for the next
 * unescaped '{' and the next unescaped '}' after it (no brace-nesting
 * awareness), and a key's value is found by an unbounded forward scan from
 * that key's own text position. This does mean a value's extraction is
 * technically not confined to "its own" object if the manifest is
 * malformed or reordered in an unexpected way -- exactly the same
 * documented characteristic the old parser had. "server" fields are
 * scanned across the whole document (not object-bounded at all) rather
 * than through the object loop below, since they are a single object, not
 * a repeated list; "hw_pin_map"/"endpoints"/"streams" entries are
 * recognized by one distinctive required key each ("hw_ep_nr"/
 * "byte_bus_id"/"rx_stream_id") as the object loop walks the document.
 *
 * Endpoint types have no universally assigned regmap.h ep_type numeric
 * constant in this codebase yet -- only ep_wakeup.h's own
 * RCP_EP_WAKEUP_EP_TYPE (0x01) is currently defined (ROADMAP.md milestone
 * 75). Rather than guess numeric assignments for endpoint types this
 * codebase has not yet pinned one down for, "ep_type" is always a raw
 * manifest-supplied integer (0-255) here, not a name this parser
 * interprets -- flagged as an honest limitation, the same "flag rather
 * than guess" convention ep_gpio.h's own RCP_EP_GPIO_WRITE_RESERVED6 note
 * follows.
 */
#ifndef RCP_CONFIG_H
#define RCP_CONFIG_H

#include "rcp/mock.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config-specific error codes, offset by 100 (matching firmware.h's
 * convention, unchanged from the old config.h) so they never collide with
 * the generic rcp_errc_t/rcp_mock_errc_t values rcp_config_apply_to_mock()
 * may also return directly (e.g. RCP_MOCK_ERR_DUPLICATE_BUS_ID if the
 * manifest names the same byte_bus_id twice). */
typedef enum {
    RCP_CFG_ERR_PARSE = 100,
} rcp_config_errc_t;

/* ── Manifest entries ──────────────────────────────────────────────────────── */

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t magic;
    uint32_t svr_implemented_options; /* RCP_REGMAP_OPT_* bitmask (regmap.h);
                                          always group-consistent (see
                                          rcp_regmap_options_group_consistent()) --
                                          this parser only ever sets both
                                          bits of a named group together */
} rcp_config_server_t;

typedef struct {
    uint8_t hw_ep_nr;
    uint8_t hw_ep_pin_nr;
    uint8_t pin_property; /* RCP_REGMAP_PIN_PROP_* bitmask (regmap.h) */
} rcp_config_hw_pin_t;

typedef struct {
    rcp_byte_bus_id_t byte_bus_id;
    uint8_t            ep_type;
    bool                ep_enable;
} rcp_config_endpoint_t;

typedef struct {
    bool     configured;
    uint64_t rx_stream_id;
} rcp_config_stream_t;

typedef struct {
    rcp_config_server_t     server;

    rcp_config_hw_pin_t    *hw_pin_map;    /* owned; free via rcp_config_manifest_free() */
    size_t                  hw_pin_map_len;

    rcp_config_endpoint_t  *endpoints;     /* owned */
    size_t                  endpoints_len;

    rcp_config_stream_t    *streams;       /* owned */
    size_t                  streams_len;
} rcp_config_manifest_t;

/* Frees m's three owned arrays and zeroes *m. Safe to call on an
 * already-freed or zero-initialized rcp_config_manifest_t. */
void rcp_config_manifest_free(rcp_config_manifest_t *m);

/* Parses json (a manifest matching the schema above) into *out. On
 * success (RCP_OK), *out is populated and must eventually be released
 * with rcp_config_manifest_free(). On failure (RCP_CFG_ERR_PARSE), *out is
 * zeroed and, if err_msg is non-NULL and err_msg_cap > 0, a NUL-terminated
 * human-readable description of the problem is written to err_msg. Every
 * field/section is optional: an empty object "{}" parses successfully into
 * an all-zero/empty manifest. */
int rcp_config_parse_json(const char *json, rcp_config_manifest_t *out, char *err_msg, size_t err_msg_cap);

/* Applies m to srv: sets srv's own regmap vendor_id/device_id/magic/
 * svr_implemented_options (rcp_mock_server_regmap()) from m->server, then
 * calls rcp_mock_server_add_endpoint() once per m->endpoints entry (with a
 * NULL handler -- this loader has no per-endpoint-type behavior of its
 * own to attach; a caller that wants one registers it afterward via
 * rcp_mock_server_add_endpoint() directly, or removes/re-adds the slot).
 * m->hw_pin_map and m->streams are parsed data only, not applied here:
 * regmap.h's own hw_pin_map/request_stream_cfg fields are
 * rcp_regmap_table_ref_t location/capacity descriptors, not backing
 * storage for individual entries (see regmap.h's file header) -- there is
 * nothing in rcp_mock_server_t for this function to write per-pin/
 * per-stream state into yet. A caller that needs that data reads
 * m->hw_pin_map/m->streams directly. Returns the first
 * rcp_mock_server_add_endpoint() error encountered (e.g.
 * RCP_MOCK_ERR_DUPLICATE_BUS_ID for a manifest naming the same
 * byte_bus_id twice), or RCP_MOCK_OK. */
rcp_mock_errc_t rcp_config_apply_to_mock(const rcp_config_manifest_t *m, rcp_mock_server_t *srv);

/* Convenience combining rcp_config_parse_json() and
 * rcp_config_apply_to_mock(): parses json and applies it to srv. Returns
 * RCP_CFG_ERR_PARSE (with err_msg filled, as above) if json is malformed,
 * or rcp_config_apply_to_mock()'s own rcp_mock_errc_t (cast to int)
 * otherwise. */
int rcp_config_load(const char *json, rcp_mock_server_t *srv, char *err_msg, size_t err_msg_cap);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CONFIG_H */
