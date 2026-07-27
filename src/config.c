#include "rcp/config.h"

#include "rcp/mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    rcp_zone_t   zone;
} zone_map_entry_t;

static const zone_map_entry_t ZONE_MAP[] = {
    {"FrontLeft",  RCP_ZONE_FRONT_LEFT},
    {"FrontRight", RCP_ZONE_FRONT_RIGHT},
    {"RearLeft",   RCP_ZONE_REAR_LEFT},
    {"RearRight",  RCP_ZONE_REAR_RIGHT},
    {"Central",    RCP_ZONE_CENTRAL},
};
#define ZONE_MAP_LEN (sizeof(ZONE_MAP) / sizeof(ZONE_MAP[0]))

void rcp_manifest_free(rcp_manifest_t *m)
{
    if (!m->zones) return;
    free(m->zones);
    m->zones     = NULL;
    m->zones_len = 0;
}

static void set_err(char *err_msg, size_t err_msg_cap, const char *fmt, const char *arg)
{
    if (err_msg && err_msg_cap > 0) {
        if (arg) snprintf(err_msg, err_msg_cap, fmt, arg);
        else     snprintf(err_msg, err_msg_cap, "%s", fmt);
    }
}

/* Bounded substring search within [start, end) -- C99 has no portable
 * memmem(), and the object substrings this parser scans are only ever a
 * small slice of the full manifest, so a simple linear scan is fine. */
static const char *find_in_range(const char *start, const char *end, const char *needle)
{
    size_t needle_len = strlen(needle);
    const char *p;

    if ((size_t)(end - start) < needle_len) return NULL;
    for (p = start; p <= end - needle_len; p++) {
        if (memcmp(p, needle, needle_len) == 0) return p;
    }
    return NULL;
}

/* Extracts the first quoted string starting the search at json+start_offset
 * (searching the *whole* remaining json, not bounded to the current
 * object -- matching cpp-RCP's own parse_json(), which has the same
 * characteristic). Returns false if no quoted string is found. */
static bool extract_str(const char *json, size_t start_offset, char *out, size_t out_cap)
{
    const char *q1 = strchr(json + start_offset, '"');
    const char *q2;
    size_t len;

    if (!q1) return false;
    q2 = strchr(q1 + 1, '"');
    if (!q2) return false;

    len = (size_t)(q2 - q1 - 1);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, q1 + 1, len);
    out[len] = '\0';
    return true;
}

//cfusa:req REQ-CFG-001
//cfusa:req REQ-CFG-002
//cfusa:req REQ-CFG-003
//cfusa:req REQ-CFG-006
int rcp_config_parse_json(const char *json, rcp_manifest_t *out, char *err_msg, size_t err_msg_cap)
{
    size_t pos = 0;
    size_t len = strlen(json);
    rcp_zone_manifest_entry_t *zones = NULL;
    size_t zones_len = 0;
    size_t zones_cap = 0;

    out->zones     = NULL;
    out->zones_len = 0;

    while (pos < len) {
        const char *open  = strchr(json + pos, '{');
        const char *close;
        const char *zk;
        const char *pk;
        char zone_name[32];
        size_t z_idx;
        bool found_zone = false;

        if (!open) break;
        close = strchr(open, '}');
        if (!close) break;

        zk = find_in_range(open, close + 1, "\"zone\"");
        if (!zk) {
            pos = (size_t)(close - json) + 1;
            continue;
        }

        if (!extract_str(json, (size_t)(zk - json) + 6, zone_name, sizeof(zone_name))) {
            set_err(err_msg, err_msg_cap, "missing string value for zone", NULL);
            free(zones);
            return RCP_CFG_ERR_PARSE;
        }

        for (z_idx = 0; z_idx < ZONE_MAP_LEN; z_idx++) {
            if (strcmp(ZONE_MAP[z_idx].name, zone_name) == 0) {
                found_zone = true;
                break;
            }
        }
        if (!found_zone) {
            set_err(err_msg, err_msg_cap, "unknown zone: %s", zone_name);
            free(zones);
            return RCP_CFG_ERR_PARSE;
        }

        if (zones_len == zones_cap) {
            size_t new_cap = (zones_cap == 0) ? 8 : zones_cap * 2;
            rcp_zone_manifest_entry_t *grown =
                (rcp_zone_manifest_entry_t *)realloc(zones, new_cap * sizeof(*grown));
            if (!grown) {
                set_err(err_msg, err_msg_cap, "out of memory", NULL);
                free(zones);
                return RCP_CFG_ERR_PARSE;
            }
            zones     = grown;
            zones_cap = new_cap;
        }
        zones[zones_len].zone        = ZONE_MAP[z_idx].zone;
        zones[zones_len].priority[0] = '\0';
        zones[zones_len].extra[0]    = '\0';

        pk = find_in_range(open, close + 1, "\"priority\"");
        if (pk) {
            extract_str(json, (size_t)(pk - json) + 10,
                        zones[zones_len].priority, sizeof(zones[zones_len].priority));
        }
        zones_len++;

        pos = (size_t)(close - json) + 1;
    }

    out->zones     = zones;
    out->zones_len = zones_len;
    return RCP_OK;
}

//cfusa:req REQ-CFG-004
//cfusa:req REQ-CFG-005
int rcp_config_load(const char *json, rcp_registry_t *reg, char *err_msg, size_t err_msg_cap)
{
    rcp_manifest_t m;
    int rc = rcp_config_parse_json(json, &m, err_msg, err_msg_cap);
    size_t i;

    if (rc != RCP_OK) return rc;

    for (i = 0; i < m.zones_len; i++) {
        rcp_controller_t *ctrl = rcp_mock_controller_new(m.zones[i].zone, NULL, NULL);
        int ec;

        if (!ctrl) {
            rcp_manifest_free(&m);
            return RCP_ERR_BUSY;
        }
        ec = rcp_registry_register(reg, ctrl);
        rcp_controller_release(ctrl); /* the registry retained its own reference */
        if (ec != RCP_OK) {
            rcp_manifest_free(&m);
            return ec;
        }
    }

    rcp_manifest_free(&m);
    return RCP_OK;
}
