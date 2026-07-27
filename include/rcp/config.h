/*
 * Zone registry loader from a JSON configuration manifest.
 *
 * rcp_config_load() parses a JSON zone-registry manifest and populates a
 * rcp_registry_t with mock controllers for each defined zone.
 *
 * Example manifest:
 *   { "zones": [
 *       { "zone": "FrontLeft",  "priority": "Normal" },
 *       { "zone": "FrontRight", "priority": "Normal" }
 *   ]}
 *
 * Deviation from cpp-RCP: cpp-RCP signals malformed input by throwing
 * config::ParseError (a std::runtime_error subclass). C has no
 * exceptions, so rcp_config_parse_json()/rcp_config_load() return
 * RCP_CFG_ERR_PARSE and write a description into a caller-supplied
 * buffer instead -- the parser itself is otherwise the same hand-rolled,
 * minimal, non-general-purpose JSON scanner cpp-RCP documents (it only
 * understands this module's own manifest schema, not arbitrary JSON).
 */
#ifndef RCP_CONFIG_H
#define RCP_CONFIG_H

#include "rcp/rcp.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config-specific error codes, offset by 100 (matching firmware.h's
 * convention) so they never collide with the generic rcp_errc_t values
 * that rcp_config_load() may also return directly (e.g.
 * RCP_ERR_ALREADY_EXISTS if the registry already has a route for a
 * manifest zone). */
typedef enum {
    RCP_CFG_ERR_PARSE = 100,
} rcp_config_errc_t;

typedef struct {
    rcp_zone_t zone;
    char        priority[16]; /* "Low"/"Normal"/"High"/"Critical", or "" if omitted */
    char        extra[64];    /* opaque metadata, currently always "" -- reserved for future manifest fields */
} rcp_zone_manifest_entry_t;

typedef struct {
    rcp_zone_manifest_entry_t *zones; /* owned; free via rcp_manifest_free() */
    size_t                       zones_len;
} rcp_manifest_t;

/* Frees m->zones and zeroes *m. Safe to call on an already-freed or
 * zero-initialized rcp_manifest_t. */
void rcp_manifest_free(rcp_manifest_t *m);

/* Parses json (a manifest matching the schema above) into *out. On
 * success (RCP_OK), *out is populated and must eventually be released
 * with rcp_manifest_free(). On failure (RCP_CFG_ERR_PARSE), *out is
 * zeroed and, if err_msg is non-NULL and err_msg_cap > 0, a NUL-terminated
 * human-readable description of the problem is written to err_msg. */
int rcp_config_parse_json(const char *json, rcp_manifest_t *out, char *err_msg, size_t err_msg_cap);

/* Parses json and registers one mock controller per zone entry into reg.
 * Returns RCP_CFG_ERR_PARSE (with err_msg filled, as above) if json is
 * malformed, or the first registration error (e.g.
 * RCP_ERR_ALREADY_EXISTS) if any zone in the manifest is already
 * registered on reg. */
int rcp_config_load(const char *json, rcp_registry_t *reg, char *err_msg, size_t err_msg_cap);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CONFIG_H */
