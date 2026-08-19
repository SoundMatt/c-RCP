/* SPDX-License-Identifier: MPL-2.0 */
/* RELAY-conformant CLI unit tests (RELAY spec §11, §12).
 *
 * Exercises rcp_cli_run() directly (no subprocess) by redirecting its
 * output/error FILE* streams to an in-memory tmpfile() and inspecting the
 * captured text. Full end-to-end schema conformance against the real
 * `relay conform --strict` tool is exercised separately in CI (see
 * .github/workflows/ci.yml's relay-conform job) against the actual c-rcp
 * binary -- these tests instead pin the specific field values rcp_cli_run()
 * must emit, which is finer-grained than schema validity alone.
 *
 * REQ-CLI-006 (main()'s own argv[1..argc)/stdout/stderr forwarding to
 * rcp_cli_run(), cli/main.c) has no dedicated test in this file, since
 * main() is a two-line, subprocess-only entry point unity's in-process
 * harness cannot invoke directly. It is tagged here rather than left
 * untagged because it is genuinely exercised end-to-end by the same
 * relay-conform CI job referenced above, which builds and runs the real
 * c-rcp binary (i.e. main() itself, not just rcp_cli_run()) against
 * `relay conform --strict`.
 */
//cfusa:test REQ-CLI-006
#include "unity.h"

#include <rcp/cli.h>
#include <rcp/rcp.h>
#include <rcp/version.h>
#include <relay/relay.h>

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Runs rcp_cli_run(argv[0..argc)) capturing stdout/stderr into out_buf/
 * err_buf (each caller-sized). Returns the exit code. */
static int run_capture(int argc, char **argv, char *out_buf, size_t out_cap,
                        char *err_buf, size_t err_cap)
{
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    int ec;
    size_t n;

    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_NOT_NULL(err);

    ec = rcp_cli_run(argc, argv, out, err);

    rewind(out);
    n = fread(out_buf, 1, out_cap - 1, out);
    out_buf[n] = '\0';
    fclose(out);

    rewind(err);
    n = fread(err_buf, 1, err_cap - 1, err);
    err_buf[n] = '\0';
    fclose(err);

    return ec;
}

/* ── version ───────────────────────────────────────────────────────────────── */

//cfusa:test REQ-CLI-001
//cfusa:test REQ-CLI-005
static void test_version_text_contains_tool_and_versions(void)
{
    char *argv[] = {"version"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "c-rcp"));
    TEST_ASSERT_NOT_NULL(strstr(out, RCP_VERSION));
    TEST_ASSERT_NOT_NULL(strstr(out, RELAY_SPEC_VERSION));
}

//cfusa:test REQ-CLI-001
//cfusa:test REQ-CLI-005
static void test_version_json_has_required_fields(void)
{
    char *argv[] = {"version", "--format", "json"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(3, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"tool\":\"c-rcp\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"protocol\":\"RCP\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"protocol_int\":5"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"version\":\"" RCP_VERSION "\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"spec_version\":\"" RELAY_SPEC_VERSION "\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"language\":\"c\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"runtime\":"));
}

/* ── capabilities ──────────────────────────────────────────────────────────── */

//cfusa:test REQ-CLI-002
static void test_capabilities_is_always_json_and_lists_mandatory_commands(void)
{
    char *argv[] = {"capabilities"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"kind\":\"capabilities\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"protocol\":\"RCP\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"protocol_int\":5"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"version\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"capabilities\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"status\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"adapt\":true"));
}

//cfusa:test REQ-CLI-002
static void test_capabilities_honestly_omits_send(void)
{
    /* This port implements only the three mandatory commands -- "send" (an
     * optional streaming extension in cpp-RCP) is not implemented, so it
     * must not be claimed here. */
    char *argv[] = {"capabilities"};
    char out[512], err[512];

    run_capture(1, argv, out, sizeof(out), err, sizeof(err));
    TEST_ASSERT_NULL(strstr(out, "\"send\""));
}

//cfusa:test REQ-CLI-002
static void test_capabilities_transports_honestly_omits_tls(void)
{
    /* c-RCP-05: tls.h/tls.c were removed outright at v0.78.0 (no
     * compat shim) -- advertising "tls" as a transport would claim a
     * backend that doesn't exist in this tree at all. */
    char *argv[] = {"capabilities"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NULL(strstr(out, "\"tls\""));
}

//cfusa:test REQ-CLI-002
static void test_capabilities_transports_lists_real_backends(void)
{
    char *argv[] = {"capabilities"};
    char out[512], err[512];
    const char *transports;

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    transports = strstr(out, "\"transports\":[");
    TEST_ASSERT_NOT_NULL(transports);
    TEST_ASSERT_NOT_NULL(strstr(transports, "\"mock\""));
    TEST_ASSERT_NOT_NULL(strstr(transports, "\"udp\""));
    TEST_ASSERT_NOT_NULL(strstr(transports, "\"shmem\""));
    TEST_ASSERT_NOT_NULL(strstr(transports, "\"tsn\""));
}

/* REQ-RMAP-030: this codebase implements trigger requests
 * (request_triggered.c) and chained requests (request_chained.c) in
 * full, but before this fix had no RCP_REGMAP_OPT_* bit -- and so no
 * "features" entry -- to advertise either one. This test pins that the
 * capabilities document's "features" array now lists all five Table 18
 * feature names, closing that previously-unadvertised-capability gap on
 * this real consumer surface (no prior test in this file inspected
 * "features" content at all). */
//cfusa:test REQ-CLI-002
static void test_capabilities_features_lists_all_five_table_18_names(void)
{
    char *argv[] = {"capabilities"};
    char out[512], err[512];
    const char *features;

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    features = strstr(out, "\"features\":[");
    TEST_ASSERT_NOT_NULL(features);
    TEST_ASSERT_NOT_NULL(strstr(features, "\"time_sync\""));
    TEST_ASSERT_NOT_NULL(strstr(features, "\"enhanced_cancel\""));
    TEST_ASSERT_NOT_NULL(strstr(features, "\"trigger\""));
    TEST_ASSERT_NOT_NULL(strstr(features, "\"chained\""));
    TEST_ASSERT_NOT_NULL(strstr(features, "\"compound_bundles\""));
}

/* ── status ────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-CLI-003
//cfusa:test REQ-CLI-005
static void test_status_json_has_required_fields(void)
{
    char *argv[] = {"status", "--format", "json"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(3, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"tool\":\"c-rcp\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"healthy\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"connected\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"endpoint\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"details\":{}"));
}

//cfusa:test REQ-CLI-003
//cfusa:test REQ-CLI-005
static void test_status_text_form(void)
{
    char *argv[] = {"status"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "healthy=true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "connected=false"));
}

/* ── error handling / exit codes (§11.3) ───────────────────────────────────── */

//cfusa:test REQ-CLI-004
static void test_no_args_returns_invalid_args(void)
{
    char out[512], err[512];
    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(0, NULL, out, sizeof(out), err, sizeof(err)));
}

//cfusa:test REQ-CLI-004
static void test_unknown_command_returns_invalid_args(void)
{
    char *argv[] = {"bogus"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "bogus"));
}

//cfusa:test REQ-CLI-004
//cfusa:test REQ-CLI-005
static void test_bad_format_value_returns_invalid_args(void)
{
    char *argv[] = {"version", "--format", "xml"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(3, argv, out, sizeof(out), err, sizeof(err)));
}

//cfusa:test REQ-CLI-004
//cfusa:test REQ-CLI-005
static void test_format_with_no_value_returns_invalid_args(void)
{
    char *argv[] = {"version", "--format"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(2, argv, out, sizeof(out), err, sizeof(err)));
}

static void test_help_returns_ok(void)
{
    char *argv[] = {"--help"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "Usage:"));
}

/* MC/DC: rcp_cli_run()'s help dispatch (src/cli.c) is a 3-condition OR --
 * strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0 ||
 * strcmp(argv[0], "help") == 0 -- and test_help_returns_ok() above only
 * ever exercises the first disjunct (paired against
 * test_unknown_command_returns_invalid_args()'s all-false case, which
 * shows "--help"'s own independence). Neither "-h" nor "help" was ever
 * passed, so operand 2 and operand 3's independent contribution to the
 * decision was undemonstrated. These two tests close that gap the same
 * way test_help_returns_ok() does, one alternate spelling each. */
static void test_help_short_flag_returns_ok(void)
{
    char *argv[] = {"-h"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "Usage:"));
}

static void test_help_word_returns_ok(void)
{
    char *argv[] = {"help"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "Usage:"));
}

/* MC/DC note (not a test): features_json()'s two `if (n < 0 ||
 * (size_t)n >= buf_len - off) return;` truncation guards (src/cli.c
 * L158, L164) each have two operands whose independence cannot be
 * demonstrated from this file, or from any other test in this
 * repository -- not merely "hasn't been", but structurally cannot be,
 * given how the function is actually invoked. features_json() is
 * `static`; its only caller is capabilities_json(), which always
 * passes a fixed `char features[128]` buffer and always passes
 * (uint8_t)RCP_CLI_IMPLEMENTED_OPTIONS -- a compile-time constant with
 * all five REQ-RMAP-030 bits set, never a runtime value a test could
 * vary. With that fixed input, the rendered string is always
 * `["time_sync","enhanced_cancel","trigger","chained",
 * "compound_bundles"]`, ~71 bytes -- nowhere near the 128-byte limit at
 * either call site (L157's lone "[" or any of L163's five "%s\"%s\""
 * writes), so `(size_t)n >= buf_len - off` is always false. And
 * snprintf() returning negative requires either a locale/multibyte
 * encoding error (none of these calls use anything but plain %s over
 * static ASCII literals) or a formatted length exceeding INT_MAX
 * (impossible for a ~71-byte result) -- so `n < 0` is always false too.
 * Every MC/DC vector this decision could ever receive through the
 * public API is `(false, false) -> false`; the "return early, truncated
 * JSON" branch is unreachable dead defensive code given this file's
 * one and only caller, not a testing gap. No fake/whitebox test is
 * added to force these -- see the task's own guidance on honest
 * unreachability over gamed coverage. */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_version_text_contains_tool_and_versions);
    RUN_TEST(test_version_json_has_required_fields);

    RUN_TEST(test_capabilities_is_always_json_and_lists_mandatory_commands);
    RUN_TEST(test_capabilities_honestly_omits_send);
    RUN_TEST(test_capabilities_transports_honestly_omits_tls);
    RUN_TEST(test_capabilities_transports_lists_real_backends);
    RUN_TEST(test_capabilities_features_lists_all_five_table_18_names);

    RUN_TEST(test_status_json_has_required_fields);
    RUN_TEST(test_status_text_form);

    RUN_TEST(test_no_args_returns_invalid_args);
    RUN_TEST(test_unknown_command_returns_invalid_args);
    RUN_TEST(test_bad_format_value_returns_invalid_args);
    RUN_TEST(test_format_with_no_value_returns_invalid_args);
    RUN_TEST(test_help_returns_ok);
    RUN_TEST(test_help_short_flag_returns_ok);
    RUN_TEST(test_help_word_returns_ok);

    return UNITY_END();
}
