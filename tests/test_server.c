//cfusa:test REQ-SRV-001
//cfusa:test REQ-SRV-002
//cfusa:test REQ-SRV-003
//cfusa:test REQ-SRV-004
//cfusa:test REQ-SRV-005
//cfusa:test REQ-SRV-006
//cfusa:test REQ-SRV-007
//cfusa:test REQ-SRV-008
//cfusa:test REQ-SRV-009
//cfusa:test REQ-SRV-010
//cfusa:test REQ-SRV-011
//cfusa:test REQ-SRV-012
//cfusa:test REQ-SRV-013
//cfusa:test REQ-SRV-014
//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
//cfusa:test REQ-SRV-017
//cfusa:test REQ-SRV-018
//cfusa:test REQ-SRV-019
//cfusa:test REQ-SRV-020
//cfusa:test REQ-SRV-021
//cfusa:test REQ-SRV-022
//cfusa:test REQ-SRV-023
//cfusa:test REQ-SRV-024
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/rcp.h>
#include <rcp/server.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Lifecycle wire values ─────────────────────────────────────────────────── */

static void test_lifecycle_wire_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0x55, RCP_SERVER_LIFECYCLE_HW_CONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0xAA, RCP_SERVER_LIFECYCLE_RCP_CONFIGURED);
}

/* ── HW_CFG_INCONSISTENT plausibility check ────────────────────────────────── */

static void test_hw_cfg_inconsistent_missing_pin_mapping(void)
{
    rcp_server_endpoint_plausibility_t eps[2] = {
        { true, true, true, false },   /* fine */
        { true, false, true, false },  /* ep_used but no pin mapping */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 2;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_HW_CFG_INCONSISTENT, rcp_server_check_hw_cfg(&snap));
}

static void test_hw_cfg_inconsistent_missing_request_stream(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = {
        { true, true, false, false }, /* pin mapped but no request stream */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_HW_CFG_INCONSISTENT, rcp_server_check_hw_cfg(&snap));
}

static void test_hw_cfg_consistent_when_satisfied(void)
{
    rcp_server_endpoint_plausibility_t eps[2] = {
        { true, true, true, false },
        { false, false, false, false }, /* unused endpoint is ignored */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 2;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK, rcp_server_check_hw_cfg(&snap));
}

static void test_hw_cfg_null_snapshot_is_inconsistent(void)
{
    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_HW_CFG_INCONSISTENT, rcp_server_check_hw_cfg(NULL));
}

/* ── RCP_CFG_INCONSISTENT plausibility check ───────────────────────────────── */

static void test_rcp_cfg_inconsistent_missing_stream_assoc(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = {
        { true, true, true, false }, /* used, but no stream/byte_bus_id assoc */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_RCP_CFG_INCONSISTENT, rcp_server_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_inconsistent_missing_response_stream(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = {
        { true, true, true, true },
    };
    rcp_server_request_stream_plausibility_t streams[1] = {
        { true, false }, /* configured, but no response stream */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_RCP_CFG_INCONSISTENT, rcp_server_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_consistent_when_satisfied(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = {
        { true, true, true, true },
    };
    rcp_server_request_stream_plausibility_t streams[2] = {
        { true, true },
        { false, false }, /* not configured -- ignored */
    };
    rcp_server_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 2;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK, rcp_server_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_null_snapshot_is_inconsistent(void)
{
    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_RCP_CFG_INCONSISTENT, rcp_server_check_rcp_cfg(NULL));
}

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

static rcp_server_plausibility_snapshot_t plausible_snapshot(rcp_server_endpoint_plausibility_t *ep,
                                                              rcp_server_request_stream_plausibility_t *rs)
{
    rcp_server_plausibility_snapshot_t snap = {0};

    *ep = (rcp_server_endpoint_plausibility_t){ true, true, true, true };
    *rs = (rcp_server_request_stream_plausibility_t){ true, true };

    snap.endpoints            = ep;
    snap.endpoint_count       = 1;
    snap.request_streams      = rs;
    snap.request_stream_count = 1;
    return snap;
}

static void test_transition_hw_unconfigured_to_hw_configured_succeeds_when_plausible(void)
{
    rcp_server_endpoint_plausibility_t ep;
    rcp_server_request_stream_plausibility_t rs;
    rcp_server_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_CONFIGURED, state);
}

static void test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = { { true, false, true, false } };
    rcp_server_plausibility_snapshot_t snap = {0};
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED;

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_HW_CFG_INCONSISTENT,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible(void)
{
    rcp_server_endpoint_plausibility_t ep;
    rcp_server_request_stream_plausibility_t rs;
    rcp_server_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_hw_configured_to_rcp_configured_fails_when_implausible(void)
{
    rcp_server_endpoint_plausibility_t eps[1] = { { true, true, true, false } };
    rcp_server_plausibility_snapshot_t snap = {0};
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_CONFIGURED;

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_RCP_CFG_INCONSISTENT,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_hw_unconfigured_is_unconditional(void)
{
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rcp_configured_to_hw_unconfigured_is_unconditional(void)
{
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_RCP_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_skipping_hw_configured(void)
{
    rcp_server_endpoint_plausibility_t ep;
    rcp_server_request_stream_plausibility_t rs;
    rcp_server_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_INVALID_TRANSITION,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_rcp_configured_to_hw_configured(void)
{
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_RCP_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_ERR_INVALID_TRANSITION,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_same_state_is_noop_success(void)
{
    rcp_server_lifecycle_t state = RCP_SERVER_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_SERVER_OK,
                       rcp_server_lifecycle_transition(&state, RCP_SERVER_LIFECYCLE_HW_CONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_LIFECYCLE_HW_CONFIGURED, state);
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

static void test_hw_unconfigured_accepts_discovery_abb_under_ntscf(void)
{
    TEST_ASSERT_TRUE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_SERVER_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_wrong_byte_bus_id(void)
{
    TEST_ASSERT_FALSE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)1u));
}

static void test_hw_unconfigured_drops_non_abb_message_type(void)
{
    TEST_ASSERT_FALSE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_SERVER_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_tscf_even_with_time_sync_supported(void)
{
    TEST_ASSERT_FALSE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_SERVER_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_tscf_without_time_sync(void)
{
    TEST_ASSERT_FALSE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_SERVER_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_configured_applies_ordinary_tscf_drop_rule(void)
{
    /* Time-sync not supported -- TSCF still dropped, matching
     * rcp_avtp_should_drop_tscf()'s own general rule. */
    TEST_ASSERT_FALSE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u));
}

static void test_hw_configured_accepts_tscf_when_time_sync_supported(void)
{
    TEST_ASSERT_TRUE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u));
}

static void test_rcp_configured_accepts_ntscf_at_any_byte_bus_id(void)
{
    /* Frame-level acceptance beyond the TSCF/time-sync rule is unrestricted
     * at this milestone; register-level write locking is a separate,
     * directly-tested concern (rcp_server_field_writable()). */
    TEST_ASSERT_TRUE(rcp_server_lifecycle_should_accept(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, (rcp_byte_bus_id_t)42u));
}

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

static void test_hw_generic_writable_only_in_hw_unconfigured(void)
{
    rcp_server_writer_ctx_t none = {0};
    rcp_server_writer_ctx_t root = { true, false };

    TEST_ASSERT_TRUE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, RCP_SERVER_FIELD_HW_GENERIC, none));
    TEST_ASSERT_FALSE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, RCP_SERVER_FIELD_HW_GENERIC, root));
    TEST_ASSERT_FALSE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, RCP_SERVER_FIELD_HW_GENERIC, root));
}

static void test_functional_w_writable_by_anyone_in_hw_configured(void)
{
    rcp_server_writer_ctx_t none = {0};

    TEST_ASSERT_FALSE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_HW_UNCONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W, none));
    TEST_ASSERT_TRUE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W, none));
}

static void test_functional_w_requires_authorized_writer_once_rcp_configured(void)
{
    rcp_server_writer_ctx_t none         = {0};
    rcp_server_writer_ctx_t owning       = { false, true };
    rcp_server_writer_ctx_t root_client  = { true, false };

    TEST_ASSERT_FALSE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W, none));
    TEST_ASSERT_TRUE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W, owning));
    TEST_ASSERT_TRUE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W, root_client));
}

static void test_functional_w_star_permanently_locked_once_rcp_configured(void)
{
    rcp_server_writer_ctx_t everyone = { true, true }; /* root client AND owning stream */

    TEST_ASSERT_TRUE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_HW_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W_STAR, everyone));
    TEST_ASSERT_FALSE(rcp_server_field_writable(
        RCP_SERVER_LIFECYCLE_RCP_CONFIGURED, RCP_SERVER_FIELD_FUNCTIONAL_W_STAR, everyone));
}

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

static void test_disabled_endpoint_queues_submitted_requests(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1, 2, 3 };
    bool executed_now;

    rcp_server_endpoint_init(&ep, false);

    executed_now = rcp_server_endpoint_submit(&ep, body, sizeof(body));

    TEST_ASSERT_FALSE(executed_now);
    TEST_ASSERT_EQUAL_UINT(1, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_enabled_endpoint_reports_immediate_execution(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 9 };
    bool executed_now;

    rcp_server_endpoint_init(&ep, true);

    executed_now = rcp_server_endpoint_submit(&ep, body, sizeof(body));

    TEST_ASSERT_TRUE(executed_now);
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_drain_one_refuses_while_disabled(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1 };
    rcp_bytes_t out = {0};

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body));

    TEST_ASSERT_FALSE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT(1, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_reenable_drains_queue_in_fifo_order(void)
{
    rcp_server_endpoint_t ep;
    uint8_t first[]  = { 0xAA };
    uint8_t second[] = { 0xBB };
    uint8_t third[]  = { 0xCC };
    rcp_bytes_t out = {0};

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, first, sizeof(first));
    (void)rcp_server_endpoint_submit(&ep, second, sizeof(second));
    (void)rcp_server_endpoint_submit(&ep, third, sizeof(third));
    TEST_ASSERT_EQUAL_UINT(3, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_set_enable(&ep, true);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xBB, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xCC, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_FALSE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_endpoint_destroy_frees_a_nonempty_queue(void)
{
    /* No crash / no leak (checked under ASan/valgrind in CI) when an
     * endpoint is destroyed with requests still queued. */
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1, 2 };

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body));
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body));

    rcp_server_endpoint_destroy(&ep);
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_server_strerror_unique_nonempty(void)
{
    const rcp_server_errc_t codes[] = {
        RCP_SERVER_OK, RCP_SERVER_ERR_HW_CFG_INCONSISTENT,
        RCP_SERVER_ERR_RCP_CFG_INCONSISTENT, RCP_SERVER_ERR_INVALID_TRANSITION,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_server_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_server_strerror(codes[j])) != 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_lifecycle_wire_values);

    RUN_TEST(test_hw_cfg_inconsistent_missing_pin_mapping);
    RUN_TEST(test_hw_cfg_inconsistent_missing_request_stream);
    RUN_TEST(test_hw_cfg_consistent_when_satisfied);
    RUN_TEST(test_hw_cfg_null_snapshot_is_inconsistent);

    RUN_TEST(test_rcp_cfg_inconsistent_missing_stream_assoc);
    RUN_TEST(test_rcp_cfg_inconsistent_missing_response_stream);
    RUN_TEST(test_rcp_cfg_consistent_when_satisfied);
    RUN_TEST(test_rcp_cfg_null_snapshot_is_inconsistent);

    RUN_TEST(test_transition_hw_unconfigured_to_hw_configured_succeeds_when_plausible);
    RUN_TEST(test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_fails_when_implausible);
    RUN_TEST(test_transition_hw_configured_to_hw_unconfigured_is_unconditional);
    RUN_TEST(test_transition_rcp_configured_to_hw_unconfigured_is_unconditional);
    RUN_TEST(test_transition_rejects_skipping_hw_configured);
    RUN_TEST(test_transition_rejects_rcp_configured_to_hw_configured);
    RUN_TEST(test_transition_same_state_is_noop_success);

    RUN_TEST(test_hw_unconfigured_accepts_discovery_abb_under_ntscf);
    RUN_TEST(test_hw_unconfigured_drops_wrong_byte_bus_id);
    RUN_TEST(test_hw_unconfigured_drops_non_abb_message_type);
    RUN_TEST(test_hw_unconfigured_drops_tscf_even_with_time_sync_supported);
    RUN_TEST(test_hw_unconfigured_drops_tscf_without_time_sync);
    RUN_TEST(test_hw_configured_applies_ordinary_tscf_drop_rule);
    RUN_TEST(test_hw_configured_accepts_tscf_when_time_sync_supported);
    RUN_TEST(test_rcp_configured_accepts_ntscf_at_any_byte_bus_id);

    RUN_TEST(test_hw_generic_writable_only_in_hw_unconfigured);
    RUN_TEST(test_functional_w_writable_by_anyone_in_hw_configured);
    RUN_TEST(test_functional_w_requires_authorized_writer_once_rcp_configured);
    RUN_TEST(test_functional_w_star_permanently_locked_once_rcp_configured);

    RUN_TEST(test_disabled_endpoint_queues_submitted_requests);
    RUN_TEST(test_enabled_endpoint_reports_immediate_execution);
    RUN_TEST(test_drain_one_refuses_while_disabled);
    RUN_TEST(test_reenable_drains_queue_in_fifo_order);
    RUN_TEST(test_endpoint_destroy_frees_a_nonempty_queue);

    RUN_TEST(test_server_strerror_unique_nonempty);

    return UNITY_END();
}
