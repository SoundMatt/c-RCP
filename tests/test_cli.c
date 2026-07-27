/* RELAY-conformant CLI unit tests (RELAY spec §11, §12).
 *
 * Exercises rcp_cli_run() directly (no subprocess) by redirecting its
 * output/error FILE* streams to an in-memory tmpfile() and inspecting the
 * captured text. Full end-to-end schema conformance against the real
 * `relay conform --strict` tool is exercised separately in CI (see
 * .github/workflows/ci.yml's relay-conform job) against the actual c-rcp
 * binary -- these tests instead pin the specific field values rcp_cli_run()
 * must emit, which is finer-grained than schema validity alone.
 */
//cfusa:test REQ-CLI-001
//cfusa:test REQ-CLI-002
//cfusa:test REQ-CLI-003
//cfusa:test REQ-CLI-004
//cfusa:test REQ-CLI-005
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

static void test_version_text_contains_tool_and_versions(void)
{
    char *argv[] = {"version"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "c-rcp"));
    TEST_ASSERT_NOT_NULL(strstr(out, RCP_VERSION));
    TEST_ASSERT_NOT_NULL(strstr(out, RELAY_SPEC_VERSION));
}

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
    TEST_ASSERT_NOT_NULL(strstr(out, "\"language\":\"cpp\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"runtime\":"));
}

/* ── capabilities ──────────────────────────────────────────────────────────── */

static void test_capabilities_is_always_json_and_lists_mandatory_commands(void)
{
    char *argv[] = {"capabilities"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"kind\":\"capabilities\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"version\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"capabilities\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"status\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"adapt\":true"));
}

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

/* ── status ────────────────────────────────────────────────────────────────── */

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

static void test_status_text_form(void)
{
    char *argv[] = {"status"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_OK, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(out, "healthy=true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "connected=false"));
}

/* ── error handling / exit codes (§11.3) ───────────────────────────────────── */

static void test_no_args_returns_invalid_args(void)
{
    char out[512], err[512];
    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(0, NULL, out, sizeof(out), err, sizeof(err)));
}

static void test_unknown_command_returns_invalid_args(void)
{
    char *argv[] = {"bogus"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(1, argv, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "bogus"));
}

static void test_bad_format_value_returns_invalid_args(void)
{
    char *argv[] = {"version", "--format", "xml"};
    char out[512], err[512];

    TEST_ASSERT_EQUAL(RCP_CLI_INVALID_ARGS, run_capture(3, argv, out, sizeof(out), err, sizeof(err)));
}

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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_version_text_contains_tool_and_versions);
    RUN_TEST(test_version_json_has_required_fields);

    RUN_TEST(test_capabilities_is_always_json_and_lists_mandatory_commands);
    RUN_TEST(test_capabilities_honestly_omits_send);

    RUN_TEST(test_status_json_has_required_fields);
    RUN_TEST(test_status_text_form);

    RUN_TEST(test_no_args_returns_invalid_args);
    RUN_TEST(test_unknown_command_returns_invalid_args);
    RUN_TEST(test_bad_format_value_returns_invalid_args);
    RUN_TEST(test_format_with_no_value_returns_invalid_args);
    RUN_TEST(test_help_returns_ok);

    return UNITY_END();
}
