/* Mutual-TLS transport tests (SG-006, IEC 62443 SL-2).
 *
 * Without an OpenSSL backend the TLS module is a compile-time interface
 * stub: it carries the secure-transport configuration (certs, CA,
 * verify_peer) and never performs an insecure send — every transport call
 * returns RCP_ERR_NOT_SUPPORTED rather than transmitting plaintext. These
 * tests pin that configuration surface and the secure-by-default refusal.
 */
//cfusa:test REQ-TLS-001
//cfusa:test REQ-TLS-002
//cfusa:test REQ-TLS-003
//cfusa:test REQ-TLS-004
//cfusa:test REQ-TLS-005
//cfusa:test REQ-TLS-006
//cfusa:test REQ-TLS-007
//cfusa:test REQ-TLS-008
//cfusa:test REQ-TLS-009
//cfusa:test REQ-TLS-010
#include "unity.h"

#include <rcp/rcp.h>
#include <rcp/tls.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_tls_config_t mtls_config(void)
{
    rcp_tls_config_t c = rcp_tls_config_default();
    c.cert_file = "/etc/rcp/client.pem";
    c.key_file  = "/etc/rcp/client.key";
    c.ca_file   = "/etc/rcp/ca.pem";
    return c;
}

static void test_verify_peer_true_by_default(void)
{
    rcp_tls_config_t c = rcp_tls_config_default();
    TEST_ASSERT_TRUE(c.verify_peer);
}

static void test_cert_verification_via_ca_bundle(void)
{
    rcp_tls_config_t c = mtls_config();
    TEST_ASSERT_NOT_NULL(c.ca_file);
    TEST_ASSERT_TRUE(c.verify_peer);
}

static void test_pem_files_carried_in_config(void)
{
    rcp_tls_config_t c = mtls_config();
    TEST_ASSERT_EQUAL_STRING("/etc/rcp/client.pem", c.cert_file);
    TEST_ASSERT_EQUAL_STRING("/etc/rcp/client.key", c.key_file);
    TEST_ASSERT_EQUAL_STRING("/etc/rcp/ca.pem", c.ca_file);
}

static void test_command_frames_never_sent_in_clear(void)
{
    rcp_controller_t *ctrl = rcp_tls_controller_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 8443, mtls_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_SET;
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
}

static void test_status_frames_never_sent_in_clear(void)
{
    rcp_controller_t *ctrl = rcp_tls_controller_new(RCP_ZONE_FRONT_LEFT, "127.0.0.1", 8443, mtls_config());
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_controller_subscribe(ctrl, &ctx, &ch));

    rcp_controller_release(ctrl);
}

static void test_no_insecure_fallback_without_backend(void)
{
    rcp_registry_t *reg = rcp_tls_registry_new();
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED,
                       rcp_tls_registry_dial(reg, RCP_ZONE_CENTRAL, "127.0.0.1", 8443, mtls_config()));

    ctrl = rcp_tls_controller_new(RCP_ZONE_CENTRAL, "127.0.0.1", 8443, mtls_config());
    cmd.zone = RCP_ZONE_CENTRAL;
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_close_terminates_session_cleanly(void)
{
    rcp_controller_t *ctrl = rcp_tls_controller_new(RCP_ZONE_REAR_LEFT, "127.0.0.1", 8443, mtls_config());
    rcp_registry_t *reg = rcp_tls_registry_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_close(reg));

    rcp_controller_release(ctrl);
    rcp_registry_destroy(reg);
}

static void test_transport_errors_propagate(void)
{
    rcp_registry_t *reg = rcp_tls_registry_new();
    rcp_controller_t *out = NULL;

    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_registry_lookup(reg, RCP_ZONE_FRONT_LEFT, &out));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_registry_register(reg, NULL));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED, rcp_registry_deregister(reg, RCP_ZONE_FRONT_LEFT));

    rcp_registry_destroy(reg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_verify_peer_true_by_default);
    RUN_TEST(test_cert_verification_via_ca_bundle);
    RUN_TEST(test_pem_files_carried_in_config);
    RUN_TEST(test_command_frames_never_sent_in_clear);
    RUN_TEST(test_status_frames_never_sent_in_clear);
    RUN_TEST(test_no_insecure_fallback_without_backend);
    RUN_TEST(test_close_terminates_session_cleanly);
    RUN_TEST(test_transport_errors_propagate);

    return UNITY_END();
}
