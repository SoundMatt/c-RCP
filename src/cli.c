/* SPDX-License-Identifier: MPL-2.0 */
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
 * satellites"; caveat added milestone 94, c-RCP-06/c-RCP-07; bit layout
 * corrected and trigger/chained added, REQ-RMAP-030): names, one per
 * group this build compiles the *API surface* for, regmap.h's own
 * svr_implemented_options bits (RCP_REGMAP_OPT_* -- five independent
 * single bits, TC18 §12.7.5 Table 18, src/regmap.c since v0.62.0,
 * corrected v0.183.0): time_sync (bit d: TSCF framing plus its
 * presentation-timestamp companion), enhanced_cancel (bit e: a
 * cancellation request plus its acknowledgement, request_cancel.h),
 * compound_bundles (bit a: a bundle header plus per-segment addressing,
 * request_compound.h), trigger (bit b, request_triggered.h) and chained
 * (bit c, request_chained.h) -- the latter two previously had no
 * advertisable bit at all despite this codebase implementing both, the
 * exact gap REQ-RMAP-030 closes. This is a static, build-time answer --
 * this generic CLI tool never connects to a live RC Server (see
 * status_json()'s own "connected":false), so there is no live
 * svr_implemented_options value to report instead of the library's own
 * compiled-in feature set.
 *
 * IMPORTANT -- "features" reports API-surface presence, not full TC18
 * wire conformance. A 2026-07-30 ecosystem audit (c-RCP-06) found, and
 * this milestone independently confirmed against the actual code, that
 * three of the five bits are not fully wire-conformant yet despite each
 * having real, working code behind it (trigger and chained have no such
 * caveat -- REQ-TRIG-nnn and REQ-CHAIN-nnn are fully closed in
 * .fusa-reqs.json):
 *   - time_sync: request_timed.c's presentation_time sub-field is a
 *     plain uint32_t, narrower than the spec's own wider field.
 *   - enhanced_cancel: request_cancel.c's encoders hard-code the shared
 *     ACF header's evt sub-field to 0 (see e.g.
 *     rcp_cancel_encode_clear_all()'s `b[5] = 0x00u`), so the
 *     acknowledge-on-filing half of the mechanism cannot be expressed on
 *     the wire.
 *   - compound_bundles: request_compound.c's repeat_count is an 8-bit,
 *     round-tripped-only sub-field (see request_compound.h's own file
 *     header) -- there is no repetition state machine actually driving
 *     repeated execution.
 * The RELAY capabilities schema's "features" array is a flat list of
 * strings with no room for a per-entry conformance caveat
 * (spec/schemas/cli-capabilities.json sets additionalProperties:false on
 * the whole document, and "features" itself is just string[]), so this
 * caveat cannot be expressed in the JSON output itself -- it is recorded
 * here, and in README.md, for anyone auditing this self-report against
 * the actual code. RCP_CLI_IMPLEMENTED_OPTIONS below is derived from
 * five independently-named per-bit predicates rather than one
 * monolithic "all" constant (c-RCP-07) precisely so that fixing any one
 * bit's conformance gap has a single, obvious place to flip its
 * predicate to reflect that -- not because any predicate's value differs
 * from the others today.
 */
#define RCP_CLI_HAS_TIME_SYNC_API        1
#define RCP_CLI_HAS_ENHANCED_CANCEL_API  1
#define RCP_CLI_HAS_COMPOUND_BUNDLES_API 1
#define RCP_CLI_HAS_TRIGGER_API          1
#define RCP_CLI_HAS_CHAINED_API          1

#define RCP_CLI_IMPLEMENTED_OPTIONS                                                          \
    ((RCP_CLI_HAS_TIME_SYNC_API        ? RCP_REGMAP_OPT_TIME_SYNC     : 0u) |                \
     (RCP_CLI_HAS_ENHANCED_CANCEL_API  ? RCP_REGMAP_OPT_ENH_CANCEL    : 0u) |                \
     (RCP_CLI_HAS_TRIGGER_API          ? RCP_REGMAP_OPT_TRIGGER       : 0u) |                \
     (RCP_CLI_HAS_CHAINED_API          ? RCP_REGMAP_OPT_CHAINED       : 0u) |                \
     (RCP_CLI_HAS_COMPOUND_BUNDLES_API ? RCP_REGMAP_OPT_COMPOUND_WAIT : 0u))

/* Renders options' implemented feature bits as a JSON array of strings
 * into buf (e.g. "[\"time_sync\",\"enhanced_cancel\"]"). A feature is
 * listed iff its own single bit is set in options -- REQ-RMAP-030's
 * five bits are independent, with no pairing/grouping invariant to
 * trust or re-check (unlike this function's own former design, back
 * when the now-removed rcp_regmap_options_group_consistent() enforced
 * one). */
static void features_json(uint8_t options, char *buf, size_t buf_len)
{
    typedef struct {
        const char *name;
        uint8_t     bit;
    } feature_bit_t;
    static const feature_bit_t FEATURES[] = {
        {"time_sync",        RCP_REGMAP_OPT_TIME_SYNC},
        {"enhanced_cancel",  RCP_REGMAP_OPT_ENH_CANCEL},
        {"trigger",          RCP_REGMAP_OPT_TRIGGER},
        {"chained",          RCP_REGMAP_OPT_CHAINED},
        {"compound_bundles", RCP_REGMAP_OPT_COMPOUND_WAIT},
    };
    size_t i;
    size_t off = 0;
    bool   first = true;
    int    n;

    if (buf_len == 0) return;

    n = snprintf(buf + off, buf_len - off, "[");
    if (n < 0 || (size_t)n >= buf_len - off) return;
    off += (size_t)n;

    for (i = 0; i < sizeof(FEATURES) / sizeof(FEATURES[0]); i++) {
        if ((options & FEATURES[i].bit) == 0) continue;
        n = snprintf(buf + off, buf_len - off, "%s\"%s\"", first ? "" : ",", FEATURES[i].name);
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

    features_json((uint8_t)RCP_CLI_IMPLEMENTED_OPTIONS, features, sizeof(features));

    /* "transports" (c-RCP-05): "tls" dropped -- tls.h/tls.c were
     * DEPRECATE-removed outright at v0.78.0 (CHANGELOG.md's Deprecation &
     * Removal Log), so it advertised a backend that doesn't exist in this
     * tree at all, let alone a working one. "mock"/"udp"/"shmem"/"tsn"
     * all have real, non-stub implementations (src/mock.c, src/udp.c,
     * src/shmem.c, src/tsn.c) and stay listed; udp.c's Windows path being
     * stub-only is a real, separate, platform-specific gap (not "this
     * transport doesn't exist" like tls was), tracked independently. */
    snprintf(buf, buf_len,
        "{"
        "\"kind\":\"capabilities\","
        "\"tool\":\"c-rcp\","
        "\"protocol\":\"RCP\","
        "\"protocol_int\":%d,"
        "\"version\":\"%s\","
        "\"spec_version\":\"%s\","
        "\"commands\":[\"version\",\"capabilities\",\"status\"],"
        "\"transports\":[\"mock\",\"udp\",\"shmem\",\"tsn\"],"
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
