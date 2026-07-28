#include "rcp/cli.h"

#include <stdbool.h>
#include <string.h>

#include "relay/relay.h"
#include "rcp/rcp.h"
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
 */
//cfusa:req REQ-CLI-002
static void capabilities_json(char *buf, size_t buf_len)
{
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
        "\"features\":[\"loaning\"],"
        "\"interfaces\":[\"Node\",\"Caller\"],"
        "\"optional_interfaces\":[],"
        "\"adapt\":true"
        "}",
        (int)RELAY_PROTOCOL_RCP, RCP_VERSION, RELAY_SPEC_VERSION);
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
