/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LIFECYCLE-001
//cfusa:test REQ-LIFECYCLE-002
//cfusa:test REQ-LIFECYCLE-003
//cfusa:test REQ-LIFECYCLE-004
//cfusa:test REQ-LIFECYCLE-005
//cfusa:test REQ-LIFECYCLE-006
//cfusa:test REQ-LIFECYCLE-007
//cfusa:test REQ-LIFECYCLE-008
//cfusa:test REQ-LIFECYCLE-009
//cfusa:test REQ-LIFECYCLE-010
//cfusa:test REQ-LIFECYCLE-011
//cfusa:test REQ-LIFECYCLE-012
//cfusa:test REQ-LIFECYCLE-013
//cfusa:test REQ-LIFECYCLE-014
//cfusa:test REQ-LIFECYCLE-015
//cfusa:test REQ-LIFECYCLE-016
//cfusa:test REQ-LIFECYCLE-017
//cfusa:test REQ-LIFECYCLE-018
//cfusa:test REQ-LIFECYCLE-019
//cfusa:test REQ-LIFECYCLE-020
//cfusa:test REQ-LIFECYCLE-021
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Lifecycle wire values ─────────────────────────────────────────────────── */

static void test_lifecycle_wire_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_LIFECYCLE_HW_UNCONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0x55, RCP_LIFECYCLE_HW_CONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0xAA, RCP_LIFECYCLE_RCP_CONFIGURED);
}

/* ── HW_CFG_INCONSISTENT plausibility check ────────────────────────────────── */

static void test_hw_cfg_inconsistent_missing_pin_mapping(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[2] = {
        { true, true, true, false },   /* fine */
        { true, false, true, false },  /* ep_used but no pin mapping */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 2;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT, rcp_lifecycle_check_hw_cfg(&snap));
}

static void test_hw_cfg_inconsistent_missing_request_stream(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, false, false }, /* pin mapped but no request stream */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT, rcp_lifecycle_check_hw_cfg(&snap));
}

static void test_hw_cfg_consistent_when_satisfied(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[2] = {
        { true, true, true, false },
        { false, false, false, false }, /* unused endpoint is ignored */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 2;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK, rcp_lifecycle_check_hw_cfg(&snap));
}

static void test_hw_cfg_null_snapshot_is_inconsistent(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT, rcp_lifecycle_check_hw_cfg(NULL));
}

/* ── RCP_CFG_INCONSISTENT plausibility check ───────────────────────────────── */

static void test_rcp_cfg_inconsistent_missing_stream_assoc(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, true, false }, /* used, but no stream/byte_bus_id assoc */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_inconsistent_missing_response_stream(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, true, true },
    };
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, false }, /* configured, but no response stream */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_consistent_when_satisfied(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, true, true },
    };
    rcp_lifecycle_request_stream_plausibility_t streams[2] = {
        { true, true },
        { false, false }, /* not configured -- ignored */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 2;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK, rcp_lifecycle_check_rcp_cfg(&snap));
}

static void test_rcp_cfg_null_snapshot_is_inconsistent(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(NULL));
}

/* ── Lifecycle transitions ─────────────────────────────────────────────────── */

static rcp_lifecycle_plausibility_snapshot_t plausible_snapshot(rcp_lifecycle_endpoint_plausibility_t *ep,
                                                              rcp_lifecycle_request_stream_plausibility_t *rs)
{
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    *ep = (rcp_lifecycle_endpoint_plausibility_t){ true, true, true, true };
    *rs = (rcp_lifecycle_request_stream_plausibility_t){ true, true };

    snap.endpoints            = ep;
    snap.endpoint_count       = 1;
    snap.request_streams      = rs;
    snap.request_stream_count = 1;
    return snap;
}

static void test_transition_hw_unconfigured_to_hw_configured_succeeds_when_plausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

static void test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, false, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_hw_configured_to_rcp_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, true, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_hw_unconfigured_is_unconditional(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rcp_configured_to_hw_unconfigured_is_unconditional(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_skipping_hw_configured(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_rcp_configured_to_hw_configured(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_same_state_is_noop_success(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

static void test_hw_unconfigured_accepts_discovery_abb_under_ntscf(void)
{
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_wrong_byte_bus_id(void)
{
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)1u));
}

static void test_hw_unconfigured_drops_non_abb_message_type(void)
{
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_tscf_even_with_time_sync_supported(void)
{
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_unconfigured_drops_tscf_without_time_sync(void)
{
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
}

static void test_hw_configured_applies_ordinary_tscf_drop_rule(void)
{
    /* Time-sync not supported -- TSCF still dropped, matching
     * rcp_avtp_should_drop_tscf()'s own general rule. */
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u));
}

static void test_hw_configured_accepts_tscf_when_time_sync_supported(void)
{
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u));
}

static void test_rcp_configured_accepts_ntscf_at_any_byte_bus_id(void)
{
    /* Frame-level acceptance beyond the TSCF/time-sync rule is unrestricted
     * at this milestone; register-level write locking is a separate,
     * directly-tested concern (rcp_lifecycle_field_writable()). */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, (rcp_byte_bus_id_t)42u));
}

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

static void test_hw_generic_writable_only_in_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t root = { true, false };

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, none));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
}

static void test_functional_w_writable_by_anyone_in_hw_configured(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
}

static void test_functional_w_requires_authorized_writer_once_rcp_configured(void)
{
    rcp_lifecycle_writer_ctx_t none         = {0};
    rcp_lifecycle_writer_ctx_t owning       = { false, true };
    rcp_lifecycle_writer_ctx_t root_client  = { true, false };

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, owning));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root_client));
}

static void test_functional_w_star_permanently_locked_once_rcp_configured(void)
{
    rcp_lifecycle_writer_ctx_t everyone = { true, true }; /* root client AND owning stream */

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, everyone));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, everyone));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_lifecycle_strerror_unique_nonempty(void)
{
    const rcp_lifecycle_errc_t codes[] = {
        RCP_LIFECYCLE_OK, RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT,
        RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_lifecycle_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_lifecycle_strerror(codes[j])) != 0);
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

    RUN_TEST(test_lifecycle_strerror_unique_nonempty);

    return UNITY_END();
}
