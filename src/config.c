/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Small hand-rolled JSON scanning helpers (see config.h's file header) ─── */

static void set_err(char *err_msg, size_t err_msg_cap, const char *fmt, const char *arg)
{
    if (err_msg && err_msg_cap > 0) {
        if (arg) snprintf(err_msg, err_msg_cap, fmt, arg);
        else     snprintf(err_msg, err_msg_cap, "%s", fmt);
    }
}

/* Bounded substring search within [start, end). */
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

/* Parses the first integer (optionally negative) found scanning forward
 * from from, skipping whitespace/colon. Returns false if none is found. */
static bool extract_uint_at(const char *from, uint64_t *out)
{
    char *end;
    unsigned long long v;

    while (*from == ' ' || *from == '\t' || *from == '\n' || *from == '\r' || *from == ':') from++;
    if (*from != '-' && (*from < '0' || *from > '9')) return false;

    v = strtoull(from, &end, 10);
    if (end == from) return false;
    *out = (uint64_t)v;
    return true;
}

/* Parses "true"/"false" found scanning forward from from, skipping
 * whitespace/colon. Returns false (leaving *out untouched) if neither
 * literal is found there. */
static bool extract_bool_at(const char *from, bool *out)
{
    while (*from == ' ' || *from == '\t' || *from == '\n' || *from == '\r' || *from == ':') from++;
    if (strncmp(from, "true", 4) == 0)  { *out = true;  return true; }
    if (strncmp(from, "false", 5) == 0) { *out = false; return true; }
    return false;
}

/* Locates the '[' ... ']' array span starting the search at key_pos
 * (typically just past a "key" match). Non-nesting: the array must contain
 * no other '[' or ']' (true for both this schema's own string arrays).
 * Returns false if either bracket is missing. */
static bool find_bracket_span(const char *key_pos, const char **out_start, const char **out_end)
{
    const char *lb = strchr(key_pos, '[');
    const char *rb;

    if (!lb) return false;
    rb = strchr(lb, ']');
    if (!rb) return false;
    *out_start = lb + 1;
    *out_end   = rb;
    return true;
}

/* ── pin_property array parsing ────────────────────────────────────────────── */

typedef struct {
    const char *name;
    uint8_t     bit;
} pin_prop_name_t;

static const pin_prop_name_t PIN_PROP_NAMES[] = {
    {"output",     RCP_REGMAP_PIN_PROP_OUTPUT},
    {"input",      RCP_REGMAP_PIN_PROP_INPUT},
    {"open_drain", RCP_REGMAP_PIN_PROP_OPEN_DRAIN},
    {"pull_up",    RCP_REGMAP_PIN_PROP_PULL_UP},
    {"pull_down",  RCP_REGMAP_PIN_PROP_PULL_DOWN},
    {"active_low", RCP_REGMAP_PIN_PROP_ACTIVE_LOW},
};
#define PIN_PROP_NAMES_LEN (sizeof(PIN_PROP_NAMES) / sizeof(PIN_PROP_NAMES[0]))

typedef struct {
    const char *name;
    uint32_t    bits;
} option_group_name_t;

static const option_group_name_t OPTION_GROUP_NAMES[] = {
    {"time_sync",        RCP_REGMAP_OPT_TIME_SYNC_TSCF | RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION},
    {"enhanced_cancel",  RCP_REGMAP_OPT_ENH_CANCEL_REQUEST | RCP_REGMAP_OPT_ENH_CANCEL_ACK},
    {"compound_bundles", RCP_REGMAP_OPT_COMPOUND_HEADER | RCP_REGMAP_OPT_COMPOUND_SEGMENT},
};
#define OPTION_GROUP_NAMES_LEN (sizeof(OPTION_GROUP_NAMES) / sizeof(OPTION_GROUP_NAMES[0]))

/* Ors into *out the bit(s)/group named by each quoted string found within
 * [start, end); unrecognized names are silently ignored (forward-
 * compatible with a future name this parser doesn't know yet, matching
 * regmap.h's own "bits outside all three groups are ignored" convention
 * for rcp_regmap_options_group_consistent()). names/name_count is one of
 * the two static tables above; bit_of selects which member to OR in. */
static void or_named_bits_u8(const char *start, const char *end, uint8_t *out,
                              const pin_prop_name_t *names, size_t name_count)
{
    const char *p = start;
    while (p < end) {
        const char *q1 = memchr(p, '"', (size_t)(end - p));
        const char *q2;
        size_t      i, len;

        if (!q1) break;
        q2 = memchr(q1 + 1, '"', (size_t)(end - (q1 + 1)));
        if (!q2) break;

        len = (size_t)(q2 - q1 - 1);
        for (i = 0; i < name_count; i++) {
            if (strlen(names[i].name) == len && memcmp(names[i].name, q1 + 1, len) == 0) {
                *out = (uint8_t)(*out | names[i].bit);
                break;
            }
        }
        p = q2 + 1;
    }
}

static void or_named_bits_u32(const char *start, const char *end, uint32_t *out,
                               const option_group_name_t *names, size_t name_count)
{
    const char *p = start;
    while (p < end) {
        const char *q1 = memchr(p, '"', (size_t)(end - p));
        const char *q2;
        size_t      i, len;

        if (!q1) break;
        q2 = memchr(q1 + 1, '"', (size_t)(end - (q1 + 1)));
        if (!q2) break;

        len = (size_t)(q2 - q1 - 1);
        for (i = 0; i < name_count; i++) {
            if (strlen(names[i].name) == len && memcmp(names[i].name, q1 + 1, len) == 0) {
                *out |= names[i].bits;
                break;
            }
        }
        p = q2 + 1;
    }
}

/* ── Manifest lifecycle ────────────────────────────────────────────────────── */

//cfusa:req REQ-CFG-013
void rcp_config_manifest_free(rcp_config_manifest_t *m)
{
    free(m->hw_pin_map);
    free(m->endpoints);
    free(m->streams);
    memset(m, 0, sizeof(*m));
}

/* Generic growable-array append, specialized per element type below rather
 * than via a void*-and-element-size generic helper: three call sites, each
 * with its own small fixed element type, is clearer than a generic
 * qsort()-style indirection for this little scanning code. */
#define DEFINE_APPEND(FN_NAME, ELEM_T)                                                        \
    static bool FN_NAME(ELEM_T **arr, size_t *len, size_t *cap, ELEM_T value)                 \
    {                                                                                          \
        if (*len == *cap) {                                                                    \
            size_t new_cap = (*cap == 0) ? 8 : *cap * 2;                                       \
            ELEM_T *grown = (ELEM_T *)realloc(*arr, new_cap * sizeof(**arr));                  \
            if (!grown) return false;                                                          \
            *arr = grown;                                                                      \
            *cap = new_cap;                                                                    \
        }                                                                                       \
        (*arr)[*len] = value;                                                                   \
        (*len)++;                                                                               \
        return true;                                                                            \
    }

DEFINE_APPEND(append_pin, rcp_config_hw_pin_t)
DEFINE_APPEND(append_endpoint, rcp_config_endpoint_t)
DEFINE_APPEND(append_stream, rcp_config_stream_t)

#undef DEFINE_APPEND

/* ── "server" object: scanned across the whole document (see file header) ─── */

static void parse_server_fields(const char *json, rcp_config_server_t *out)
{
    const char *k;
    uint64_t    v;

    memset(out, 0, sizeof(*out));

    k = strstr(json, "\"vendor_id\"");
    if (k && extract_uint_at(k + strlen("\"vendor_id\""), &v)) out->vendor_id = (uint16_t)v;

    k = strstr(json, "\"device_id\"");
    if (k && extract_uint_at(k + strlen("\"device_id\""), &v)) out->device_id = (uint16_t)v;

    k = strstr(json, "\"magic\"");
    if (k && extract_uint_at(k + strlen("\"magic\""), &v)) out->magic = (uint32_t)v;

    k = strstr(json, "\"svr_implemented_options\"");
    if (k) {
        const char *arr_start, *arr_end;
        if (find_bracket_span(k + strlen("\"svr_implemented_options\""), &arr_start, &arr_end)) {
            or_named_bits_u32(arr_start, arr_end, &out->svr_implemented_options,
                               OPTION_GROUP_NAMES, OPTION_GROUP_NAMES_LEN);
        }
    }
}

/* ── Entry object parsing (the non-nesting object loop; see file header) ──── */

//cfusa:req REQ-CFG-001
//cfusa:req REQ-CFG-002
//cfusa:req REQ-CFG-003
static bool parse_pin_entry(const char *open, const char *close, rcp_config_hw_pin_t *out,
                             char *err_msg, size_t err_msg_cap)
{
    const char *k;
    uint64_t    v;

    memset(out, 0, sizeof(*out));

    k = find_in_range(open, close + 1, "\"hw_ep_nr\"");
    if (!k || !extract_uint_at(k + strlen("\"hw_ep_nr\""), &v)) {
        set_err(err_msg, err_msg_cap, "hw_pin_map entry missing numeric hw_ep_nr", NULL);
        return false;
    }
    out->hw_ep_nr = (uint8_t)v;

    k = find_in_range(open, close + 1, "\"hw_ep_pin_nr\"");
    if (!k || !extract_uint_at(k + strlen("\"hw_ep_pin_nr\""), &v)) {
        set_err(err_msg, err_msg_cap, "hw_pin_map entry missing numeric hw_ep_pin_nr", NULL);
        return false;
    }
    out->hw_ep_pin_nr = (uint8_t)v;

    k = find_in_range(open, close + 1, "\"pin_property\"");
    if (k) {
        const char *arr_start, *arr_end;
        if (find_bracket_span(k + strlen("\"pin_property\""), &arr_start, &arr_end) &&
            arr_end <= close) {
            or_named_bits_u8(arr_start, arr_end, &out->pin_property, PIN_PROP_NAMES, PIN_PROP_NAMES_LEN);
        }
    }
    return true;
}

//cfusa:req REQ-CFG-004
//cfusa:req REQ-CFG-005
static bool parse_endpoint_entry(const char *open, const char *close, rcp_config_endpoint_t *out,
                                  char *err_msg, size_t err_msg_cap)
{
    const char *k;
    uint64_t    v;
    bool        b;

    memset(out, 0, sizeof(*out));

    k = find_in_range(open, close + 1, "\"byte_bus_id\"");
    if (!k || !extract_uint_at(k + strlen("\"byte_bus_id\""), &v)) {
        set_err(err_msg, err_msg_cap, "endpoint entry missing numeric byte_bus_id", NULL);
        return false;
    }
    out->byte_bus_id = (rcp_byte_bus_id_t)v;

    k = find_in_range(open, close + 1, "\"ep_type\"");
    if (!k || !extract_uint_at(k + strlen("\"ep_type\""), &v)) {
        set_err(err_msg, err_msg_cap, "endpoint entry missing numeric ep_type", NULL);
        return false;
    }
    out->ep_type = (uint8_t)v;

    k = find_in_range(open, close + 1, "\"ep_enable\"");
    if (k && extract_bool_at(k + strlen("\"ep_enable\""), &b)) out->ep_enable = b;

    return true;
}

//cfusa:req REQ-CFG-006
static bool parse_stream_entry(const char *open, const char *close, rcp_config_stream_t *out,
                                char *err_msg, size_t err_msg_cap)
{
    const char *k;
    uint64_t    v;
    bool        b;

    memset(out, 0, sizeof(*out));
    out->configured = true; /* default: an entry that exists at all is configured */

    k = find_in_range(open, close + 1, "\"rx_stream_id\"");
    if (!k || !extract_uint_at(k + strlen("\"rx_stream_id\""), &v)) {
        set_err(err_msg, err_msg_cap, "stream entry missing numeric rx_stream_id", NULL);
        return false;
    }
    out->rx_stream_id = v;

    k = find_in_range(open, close + 1, "\"configured\"");
    if (k && extract_bool_at(k + strlen("\"configured\""), &b)) out->configured = b;

    return true;
}

//cfusa:req REQ-CFG-007
int rcp_config_parse_json(const char *json, rcp_config_manifest_t *out, char *err_msg, size_t err_msg_cap)
{
    size_t pos = 0;
    size_t len = strlen(json);

    rcp_config_hw_pin_t   *pins      = NULL; size_t pins_len = 0, pins_cap = 0;
    rcp_config_endpoint_t *endpoints = NULL; size_t eps_len  = 0, eps_cap  = 0;
    rcp_config_stream_t   *streams   = NULL; size_t strs_len = 0, strs_cap = 0;

    memset(out, 0, sizeof(*out));
    parse_server_fields(json, &out->server);

    while (pos < len) {
        const char *open  = strchr(json + pos, '{');
        const char *close;

        if (!open) break;
        close = strchr(open, '}');
        if (!close) break;

        if (find_in_range(open, close + 1, "\"byte_bus_id\"")) {
            rcp_config_endpoint_t entry;
            if (!parse_endpoint_entry(open, close, &entry, err_msg, err_msg_cap)) goto fail;
            if (!append_endpoint(&endpoints, &eps_len, &eps_cap, entry)) {
                set_err(err_msg, err_msg_cap, "out of memory", NULL);
                goto fail;
            }
        } else if (find_in_range(open, close + 1, "\"hw_ep_nr\"")) {
            rcp_config_hw_pin_t entry;
            if (!parse_pin_entry(open, close, &entry, err_msg, err_msg_cap)) goto fail;
            if (!append_pin(&pins, &pins_len, &pins_cap, entry)) {
                set_err(err_msg, err_msg_cap, "out of memory", NULL);
                goto fail;
            }
        } else if (find_in_range(open, close + 1, "\"rx_stream_id\"") ||
                   find_in_range(open, close + 1, "\"configured\"")) {
            /* "configured" alone (rx_stream_id missing/malformed) still
             * routes here so a stream entry missing its one required field
             * is rejected by parse_stream_entry() below, rather than
             * silently skipped as an unrecognized object. */
            rcp_config_stream_t entry;
            if (!parse_stream_entry(open, close, &entry, err_msg, err_msg_cap)) goto fail;
            if (!append_stream(&streams, &strs_len, &strs_cap, entry)) {
                set_err(err_msg, err_msg_cap, "out of memory", NULL);
                goto fail;
            }
        }
        /* Any other object (including the "server" section's own span, and
         * the manifest's outer wrapper) is not one of this schema's three
         * repeated entry kinds -- skipped, not an error (see file header). */

        pos = (size_t)(close - json) + 1;
    }

    out->hw_pin_map     = pins;
    out->hw_pin_map_len = pins_len;
    out->endpoints      = endpoints;
    out->endpoints_len  = eps_len;
    out->streams        = streams;
    out->streams_len    = strs_len;
    return RCP_OK;

fail:
    free(pins);
    free(endpoints);
    free(streams);
    memset(out, 0, sizeof(*out));
    return RCP_CFG_ERR_PARSE;
}

//cfusa:req REQ-CFG-008
//cfusa:req REQ-CFG-009
rcp_mock_errc_t rcp_config_apply_to_mock(const rcp_config_manifest_t *m, rcp_mock_server_t *srv)
{
    rcp_regmap_general_t *map = rcp_mock_server_regmap(srv);
    size_t                 i;

    map->vendor_id                = m->server.vendor_id;
    map->device_id                = m->server.device_id;
    if (m->server.magic != 0) map->magic = m->server.magic;
    map->svr_implemented_options |= m->server.svr_implemented_options;

    for (i = 0; i < m->endpoints_len; i++) {
        const rcp_config_endpoint_t *ep = &m->endpoints[i];
        rcp_mock_errc_t ec = rcp_mock_server_add_endpoint(srv, ep->byte_bus_id, ep->ep_type,
                                                           ep->ep_enable, NULL, NULL);
        if (ec != RCP_MOCK_OK) return ec;
    }
    return RCP_MOCK_OK;
}

//cfusa:req REQ-CFG-010
int rcp_config_load(const char *json, rcp_mock_server_t *srv, char *err_msg, size_t err_msg_cap)
{
    rcp_config_manifest_t m;
    int rc = rcp_config_parse_json(json, &m, err_msg, err_msg_cap);
    rcp_mock_errc_t ec;

    if (rc != RCP_OK) return rc;

    ec = rcp_config_apply_to_mock(&m, srv);
    if (ec != RCP_MOCK_OK) {
        set_err(err_msg, err_msg_cap, rcp_mock_strerror(ec), NULL);
    }

    rcp_config_manifest_free(&m);
    return (int)ec;
}
