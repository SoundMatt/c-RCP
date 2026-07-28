#include "rcp/cli.h"

#include <stdbool.h>
#include <string.h>

#include "relay/relay.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/version.h"

/* runtime_string returns the compiler id + major version (§12.1 "runtime"). */
static const char *runtime_string(void)
{
    static char buf[32];
#if defined(__clang__)
    snprintf(buf, sizeof(buf), "clang-%d", __clang_major__);
#elif defined(__GNUC__)
    snprintf(buf, sizeof(buf), "gcc-%d", __GNUC__);
#elif defined(_MSC_VER)
    snprintf(buf, sizeof(buf), "msvc-%d", _MSC_VER);
#else
    snprintf(buf, sizeof(buf), "unknown");
#endif
    return buf;
}

/* ── §12.1 version document ────────────────────────────────────────────────── *
 *
 * "language" was "cpp" through RELAY spec 1.11: spec/schemas/cli-version.json's
 * enum only accepted "go"|"cpp"|"rust", with no accommodation for a pure-C
 * implementation, and a literal "c" would have hard-FAILed `relay conform
 * --strict` (confirmed by reading the schema directly). RELAY v1.12 added
 * "c" to that enum (github.com/SoundMatt/RELAY PR #61) -- this now reports
 * the accurate value.
 */
//cfusa:req REQ-CLI-001
static void version_json(char *buf, size_t buf_len)
{
    snprintf(buf, buf_len,
        "{"
        "\"tool\":\"c-rcp\","
        "\"protocol\":\"RCP\","
        "\"protocol_int\":%d,"
        "\"version\":\"%s\","
        "\"spec_version\":\"%s\","
        "\"language\":\"c\","
        "\"runtime\":\"%s\""
        "}",
        (int)RELAY_PROTOCOL_RCP, RCP_VERSION, RELAY_SPEC_VERSION, runtime_string());
}

static void version_text(char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "c-rcp %s (RCP, RELAY spec %s, c, %s)",
        RCP_VERSION, RELAY_SPEC_VERSION, runtime_string());
}

/* ── §12.2 capabilities document (always JSON) ────────────────────────────── *
 *
 * "commands" honestly omits "send": this port implements only the three
 * mandatory commands (see cli.h's scope note). "adapt":true is accurate --
 * rcp_adapt() (v0.46.0) is a real, working Adapt() implementation.
 * "protocol"/"protocol_int" mirror version_json()'s values -- §12.2's
 * worked example includes them for single-protocol tools like this one.
 *
 * "features" (ROADMAP.md milestone 77, "Foundational test/config
 * satellites"): before this milestone this listed a legacy-satellite-
 * flavored ["loaning"] entry. It now instead names, one per group this
 * build actually implements, regmap.h's own svr_implemented_options
 * feature-bundle groups (RCP_REGMAP_OPT_* -- "three all-or-nothing feature
 * groups", src/regmap.c since v0.62.0): time_sync (TSCF framing plus its
 * presentation-timestamp companion), enhanced_cancel (a cancellation
 * request plus its acknowledgement, request_cancel.h), compound_bundles
 * (a bundle header plus per-segment addressing, request_compound.h). This
 * is a static, build-time answer -- this generic CLI tool never connects
 * to a live RC Server (see status_json()'s own "connected":false), so
 * there is no live svr_implemented_options value to report instead of the
 * library's own implemented feature set. RCP_CLI_ALL_IMPLEMENTED_OPTIONS
 * sets every bit in all three groups together, so it is always
 * rcp_regmap_options_group_consistent() by construction.
 */
#define RCP_CLI_ALL_IMPLEMENTED_OPTIONS                                        \
    (RCP_REGMAP_OPT_TIME_SYNC_TSCF | RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION |    \
     RCP_REGMAP_OPT_ENH_CANCEL_REQUEST | RCP_REGMAP_OPT_ENH_CANCEL_ACK |        \
     RCP_REGMAP_OPT_COMPOUND_HEADER | RCP_REGMAP_OPT_COMPOUND_SEGMENT)

/* Renders options' implemented feature-bundle groups as a JSON array of
 * strings into buf (e.g. "[\"time_sync\",\"enhanced_cancel\"]"). A group is
 * listed iff every bit belonging to it is set in options; per
 * rcp_regmap_options_group_consistent()'s own all-or-nothing invariant, a
 * caller-supplied options value would never set only some of a group's
 * bits, but this function checks the full group mask regardless, rather
 * than trusting that invariant silently. */
static void features_json(uint32_t options, char *buf, size_t buf_len)
{
    typedef struct {
        const char *name;
        uint32_t    group;
    } feature_group_t;
    static const feature_group_t GROUPS[] = {
        {"time_sync",        RCP_REGMAP_OPT_TIME_SYNC_TSCF | RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION},
        {"enhanced_cancel",  RCP_REGMAP_OPT_ENH_CANCEL_REQUEST | RCP_REGMAP_OPT_ENH_CANCEL_ACK},
        {"compound_bundles", RCP_REGMAP_OPT_COMPOUND_HEADER | RCP_REGMAP_OPT_COMPOUND_SEGMENT},
    };
    size_t i;
    size_t off = 0;
    bool   first = true;
    int    n;

    if (buf_len == 0) return;

    n = snprintf(buf + off, buf_len - off, "[");
    if (n < 0 || (size_t)n >= buf_len - off) return;
    off += (size_t)n;

    for (i = 0; i < sizeof(GROUPS) / sizeof(GROUPS[0]); i++) {
        if ((options & GROUPS[i].group) != GROUPS[i].group) continue;
        n = snprintf(buf + off, buf_len - off, "%s\"%s\"", first ? "" : ",", GROUPS[i].name);
        if (n < 0 || (size_t)n >= buf_len - off) return; /* truncated: leave buf as-is (invalid JSON,
                                                              but never an out-of-bounds write) */
        off += (size_t)n;
        first = false;
    }
    snprintf(buf + off, buf_len - off, "]");
}

//cfusa:req REQ-CLI-002
static void capabilities_json(char *buf, size_t buf_len)
{
    char features[128];

    features_json((uint32_t)RCP_CLI_ALL_IMPLEMENTED_OPTIONS, features, sizeof(features));

    snprintf(buf, buf_len,
        "{"
        "\"kind\":\"capabilities\","
        "\"tool\":\"c-rcp\","
        "\"protocol\":\"RCP\","
        "\"protocol_int\":%d,"
        "\"version\":\"%s\","
        "\"spec_version\":\"%s\","
        "\"commands\":[\"version\",\"capabilities\",\"status\"],"
        "\"transports\":[\"mock\",\"udp\",\"shmem\",\"tls\",\"tsn\"],"
        "\"features\":%s,"
        "\"interfaces\":[\"Node\",\"Caller\"],"
        "\"optional_interfaces\":[],"
        "\"adapt\":true"
        "}",
        (int)RELAY_PROTOCOL_RCP, RCP_VERSION, RELAY_SPEC_VERSION, features);
}

/* ── §12.3 status document ────────────────────────────────────────────────── */

//cfusa:req REQ-CLI-003
static void status_json(char *buf, size_t buf_len)
{
    snprintf(buf, buf_len,
        "{"
        "\"protocol\":\"RCP\","
        "\"tool\":\"c-rcp\","
        "\"version\":\"%s\","
        "\"healthy\":true,"
        "\"connected\":false,"
        "\"endpoint\":\"\","
        "\"details\":{}"
        "}",
        RCP_VERSION);
}

static void status_text(char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "c-rcp: healthy=true connected=false endpoint=");
}

/* ── usage ─────────────────────────────────────────────────────────────────── */

static const char *usage(void)
{
    return
        "Usage: c-rcp <command> [--format text|json]\n"
        "Commands:\n"
        "  version            Print tool and spec version\n"
        "  capabilities       Print the RELAY capabilities document (JSON)\n"
        "  status             Print self-assessed health\n";
}

/* format_is_json returns true if --format json appears in argv (from index
 * 1). Returns false for text/absent. Sets *bad to true on an unrecognised
 * --format value or a --format with no following value. */
static bool format_is_json(int argc, char **argv, bool *bad)
{
    int i;
    *bad = false;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                *bad = true;
                return false;
            }
            if (strcmp(argv[i + 1], "json") == 0) return true;
            if (strcmp(argv[i + 1], "text") == 0) return false;
            *bad = true;
            return false;
        }
    }
    return false; /* default: text */
}

//cfusa:req REQ-CLI-004
//cfusa:req REQ-CLI-005
int rcp_cli_run(int argc, char **argv, FILE *out, FILE *err)
{
    char doc[512];
    bool bad, json;

    if (argc < 1) {
        fputs(usage(), err);
        return RCP_CLI_INVALID_ARGS;
    }

    if (strcmp(argv[0], "version") == 0) {
        bad = false;
        json = format_is_json(argc, argv, &bad);
        if (bad) {
            fputs("error: invalid --format\n", err);
            return RCP_CLI_INVALID_ARGS;
        }
        if (json) version_json(doc, sizeof(doc));
        else      version_text(doc, sizeof(doc));
        fprintf(out, "%s\n", doc);
        return RCP_CLI_OK;
    }

    if (strcmp(argv[0], "capabilities") == 0) {
        /* Always JSON; no --format flag (§11.1). */
        capabilities_json(doc, sizeof(doc));
        fprintf(out, "%s\n", doc);
        return RCP_CLI_OK;
    }

    if (strcmp(argv[0], "status") == 0) {
        bad = false;
        json = format_is_json(argc, argv, &bad);
        if (bad) {
            fputs("error: invalid --format\n", err);
            return RCP_CLI_INVALID_ARGS;
        }
        if (json) status_json(doc, sizeof(doc));
        else      status_text(doc, sizeof(doc));
        fprintf(out, "%s\n", doc);
        return RCP_CLI_OK; /* always healthy -> 0 (§11.1: 1 if degraded) */
    }

    if (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0 ||
        strcmp(argv[0], "help") == 0) {
        fputs(usage(), out);
        return RCP_CLI_OK;
    }

    fprintf(err, "error: unknown command '%s'\n%s", argv[0], usage());
    return RCP_CLI_INVALID_ARGS;
}
