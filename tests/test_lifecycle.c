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
//cfusa:test REQ-WIREERR-004
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
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for this transition */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

static void test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, false, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for this transition */

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_hw_configured_to_rcp_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, true, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */
}

static void test_transition_hw_configured_to_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger  = {0};
    rcp_lifecycle_writer_ctx_t discovery = {false, false, false, true};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, stranger));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, discovery));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_hw_configured_to_hw_unconfigured_is_unconditional_once_authorized(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger = {0};
    rcp_lifecycle_writer_ctx_t root     = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, stranger));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rcp_configured_to_hw_unconfigured_is_unconditional_once_authorized(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger = {0};
    rcp_lifecycle_writer_ctx_t discovery = {false, false, false, true};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, stranger));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, discovery));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_skipping_hw_configured(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    /* Invalid-transition-topology is rejected before writer authorization
     * is even consulted -- true regardless of writer, asserted here with
     * an authorized one to isolate the topology check specifically. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

static void test_transition_rejects_rcp_configured_to_hw_configured(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, root));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

static void test_transition_same_state_is_noop_success(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for a no-op */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, none));
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

/* As of the REQ-LIFECYCLE-028 fix (TC18 §12.3.1.2, issue #198), a
 * TSCF-headed AVTPDU is dropped in HW_CONFIGURED unconditionally --
 * regardless of time_sync_supported, the same rule already applied to
 * HW_UNCONFIGURED -- not merely subject to the general time-sync rule.
 * This function's own name is now the opposite of what it asserts: kept
 * (renamed) rather than deleted, since the previous, more permissive
 * behavior was this module's original (pre-gap-audit) design and is
 * worth a test explicitly pinning that it no longer holds. */
static void test_hw_configured_drops_tscf_even_when_time_sync_supported(void)
{
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(
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

/* As of the REQ-LIFECYCLE-030/036 fix, HW_CONFIGURED is no longer
 * writable by any writer unconditionally: TC18 §12.3.1.2/§12.7.3
 * require the same root-client/owning-stream authorization RCP_CONFIGURED
 * already applies, plus a discovery-stream alternative neither state
 * modeled before this fix. */
static void test_functional_w_not_writable_in_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
}

static void test_functional_w_hw_configured_requires_authorization_or_discovery_stream(void)
{
    rcp_lifecycle_writer_ctx_t none          = {0};
    rcp_lifecycle_writer_ctx_t via_ep0       = {0};
    rcp_lifecycle_writer_ctx_t via_stream    = {0};
    rcp_lifecycle_writer_ctx_t via_discovery = {0};

    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, via_ep0));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, via_stream));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, via_discovery));
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

/* REQ-LIFECYCLE-027: TC18 §12.3.1.1/§12.3.1.2/§12.3.1.3 each require a
 * write request be accepted only when carried in a unicast frame, once
 * per lifecycle state. Verified across all three field kinds and every
 * state/authorization combination that would otherwise be writable --
 * via_non_unicast_frame alone flips each of these from writable to
 * unwritable, with every other input held identical to an already-
 * writable case above. */
static void test_field_writable_denies_non_unicast_frame_regardless_of_kind_or_authorization(void)
{
    rcp_lifecycle_writer_ctx_t hw_generic_multicast  = { false, false, true };
    rcp_lifecycle_writer_ctx_t functional_w_multicast = { false, false, true };
    rcp_lifecycle_writer_ctx_t root_client_multicast  = { true, false, true }; /* root client
                                                                                   AND non-
                                                                                   unicast */
    rcp_lifecycle_writer_ctx_t w_star_multicast       = { true, true, true }; /* fully
                                                                                  authorized AND
                                                                                  non-unicast */

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, hw_generic_multicast));

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, functional_w_multicast));

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root_client_multicast));

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, w_star_multicast));
}

/* REQ-LIFECYCLE-024 / REQ-WIREERR-004: rcp_lifecycle_field_write_error()
 * maps every denial rcp_lifecycle_field_writable() reports to RCP_ERROR_UNAUTHORIZED_ACCESS
 * -- per TC18 §13.7.1.2's own concrete example -- and RCP_ERROR_NONE
 * when writable, regardless of *why* a denial occurred (state alone, or
 * writer/frame on top of an otherwise-permitting state). Exercises one
 * case of each: state-only (HW_GENERIC past HW_UNCONFIGURED, even a
 * maximally-authorized writer), writer-only (FUNCTIONAL_W in
 * RCP_CONFIGURED from an unauthorized writer), and non-unicast-only
 * (REQ-LIFECYCLE-027, an otherwise-authorized writer on a non-unicast
 * frame) -- all three collapse to the same wire code, matching the
 * single "write prohibited register" case TC18 itself describes. */
static void test_field_write_error_maps_every_denial_to_unauthorized_access(void)
{
    rcp_lifecycle_writer_ctx_t root           = { true, false, false, false };
    rcp_lifecycle_writer_ctx_t stranger        = { false, false, false, false };
    rcp_lifecycle_writer_ctx_t root_multicast  = { true, false, true, false };

    /* Writable: RCP_ERROR_NONE. */
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));

    /* State-only denial: even root can't write HW_GENERIC once past
     * HW_UNCONFIGURED. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));

    /* Writer-only denial: RCP_CONFIGURED would permit FUNCTIONAL_W for an
     * authorized writer, but stranger isn't one. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));

    /* Non-unicast-only denial: root would otherwise be authorized. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root_multicast));

    /* RCP_ERROR_LOCKED_MEM_ACCESS is never emitted by this function --
     * see its own header doc comment for why. */
    TEST_ASSERT_NOT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
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
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_requires_authorization);
    RUN_TEST(test_transition_hw_configured_to_hw_unconfigured_is_unconditional_once_authorized);
    RUN_TEST(test_transition_rcp_configured_to_hw_unconfigured_is_unconditional_once_authorized);
    RUN_TEST(test_transition_rejects_skipping_hw_configured);
    RUN_TEST(test_transition_rejects_rcp_configured_to_hw_configured);
    RUN_TEST(test_transition_same_state_is_noop_success);

    RUN_TEST(test_hw_unconfigured_accepts_discovery_abb_under_ntscf);
    RUN_TEST(test_hw_unconfigured_drops_wrong_byte_bus_id);
    RUN_TEST(test_hw_unconfigured_drops_non_abb_message_type);
    RUN_TEST(test_hw_unconfigured_drops_tscf_even_with_time_sync_supported);
    RUN_TEST(test_hw_unconfigured_drops_tscf_without_time_sync);
    RUN_TEST(test_hw_configured_applies_ordinary_tscf_drop_rule);
    RUN_TEST(test_hw_configured_drops_tscf_even_when_time_sync_supported);
    RUN_TEST(test_rcp_configured_accepts_ntscf_at_any_byte_bus_id);

    RUN_TEST(test_hw_generic_writable_only_in_hw_unconfigured);
    RUN_TEST(test_functional_w_not_writable_in_hw_unconfigured);
    RUN_TEST(test_functional_w_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_w_requires_authorized_writer_once_rcp_configured);
    RUN_TEST(test_functional_w_star_permanently_locked_once_rcp_configured);
    RUN_TEST(test_field_writable_denies_non_unicast_frame_regardless_of_kind_or_authorization);
    RUN_TEST(test_field_write_error_maps_every_denial_to_unauthorized_access);

    RUN_TEST(test_lifecycle_strerror_unique_nonempty);

    return UNITY_END();
}
