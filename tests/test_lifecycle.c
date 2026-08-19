/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-WIREERR-004
//cfusa:test REQ-RMAP-025
//cfusa:test REQ-AVTP-029
// Security-relevant subset (CYBERSECURITY.md §1.5, Layer 5 —
// Register-Map Write Authorization): HW_GENERIC/FUNCTIONAL_W/
// FUNCTIONAL_W_STAR field-locking by lifecycle state, paired with
// regmap.c's writer-authority check. See CYBERSECURITY.md; formally
// verified via tla/LifecycleStateMachine.tla's
// FieldLockMonotonicWhileConfigured. Each test function below carries
// its own security-test marker alongside its regular test marker where
// relevant, placed at that function instead of listed here.
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Lifecycle wire values ─────────────────────────────────────────────────── */

//cfusa:test REQ-LIFECYCLE-001
static void test_lifecycle_wire_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, RCP_LIFECYCLE_HW_UNCONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0x55, RCP_LIFECYCLE_HW_CONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0xAA, RCP_LIFECYCLE_RCP_CONFIGURED);
}

/* ── HW_CFG_INCONSISTENT plausibility check ────────────────────────────────── */

//cfusa:test REQ-LIFECYCLE-002
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

//cfusa:test REQ-LIFECYCLE-003
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

//cfusa:test REQ-LIFECYCLE-004
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

//cfusa:test REQ-LIFECYCLE-004
static void test_hw_cfg_null_snapshot_is_inconsistent(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT, rcp_lifecycle_check_hw_cfg(NULL));
}

/* ── RCP_CFG_INCONSISTENT plausibility check ───────────────────────────────── */

//cfusa:test REQ-LIFECYCLE-005
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

//cfusa:test REQ-LIFECYCLE-006
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

//cfusa:test REQ-LIFECYCLE-007
static void test_rcp_cfg_consistent_when_satisfied(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, true, true },
    };
    rcp_lifecycle_request_stream_plausibility_t streams[2] = {
        { true, true, 0 }, /* response_stream_index 0 -- names snap's own slot 0 below */
        { false, false, 0 }, /* not configured -- ignored */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 2;
    snap.response_stream_count = 1; /* REQ-RMAP-049: streams[0]'s own response_stream_index (0) is valid */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* MC/DC closure: request_streams_consistent()'s own "ep->ep_used &&
 * ep->has_stream_assoc && ep->request_stream_index == i" only ever
 * reaches its own has_stream_assoc condition through
 * test_rcp_cfg_consistent_when_satisfied above, which always sets it
 * true -- its own independent effect (an in-use endpoint that is
 * otherwise eligible, ep_used and request_stream_index both matching,
 * but genuinely has no stream association) was never demonstrated.
 * REQ-LIFECYCLE-038's own bullet-3 rule exists specifically to catch
 * this: an orphaned, unbound stream must be flagged inconsistent even
 * when a same-indexed endpoint superficially looks like a match. */
static void test_rcp_cfg_inconsistent_endpoint_lacks_stream_assoc_despite_matching_index(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = {
        { true, true, true, false, 0 }, /* ep_used, request_stream_index==0 -- but
                                            has_stream_assoc is false */
    };
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 0 },
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

//cfusa:test REQ-LIFECYCLE-007
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
    *rs = (rcp_lifecycle_request_stream_plausibility_t){ true, true, 0 };

    snap.endpoints             = ep;
    snap.endpoint_count        = 1;
    snap.request_streams       = rs;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1; /* REQ-RMAP-049: rs's own response_stream_index (0) is valid */
    return snap;
}

//cfusa:test REQ-LIFECYCLE-008
static void test_transition_hw_unconfigured_to_hw_configured_succeeds_when_plausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for this transition */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

/* CORRECTED (issue #455): a pixel-level re-transcription of TC18 Figure
 * 17 (page 51 of the current RC5 PDF) found that the HW_UNCONFIGURED ->
 * HW_CONFIGURED advance's own label -- "Request on discovery stream to
 * set HW_CONFIGURED state & HW_config consistent -> send positive
 * response" -- makes no mention of endpoint idleness at all. The "all
 * other EPs are Idle" wording this test previously pinned as this
 * transition's own gate in fact belongs to a DIFFERENT arrow -- the
 * RCP_CONFIGURED -> HW_CONFIGURED demotion's own label (see
 * test_transition_rcp_configured_to_hw_configured_requires_idle() below,
 * REQ-LIFECYCLE-022's real home). This is now a regression test proving
 * the OLD, misapplied gate is gone: a request with all_other_eps_idle
 * false now SUCCEEDS for this specific transition, where it previously,
 * incorrectly, failed with RCP_LIFECYCLE_ERR_EPS_NOT_IDLE. */
//cfusa:test REQ-LIFECYCLE-008
static void test_transition_hw_unconfigured_to_hw_configured_not_idle_gated(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none, false));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

//cfusa:test REQ-LIFECYCLE-008
static void test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, false, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for this transition */

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_HW_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, &snap, none, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state); /* unchanged */
}

//cfusa:test REQ-LIFECYCLE-009
static void test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

//cfusa:test REQ-LIFECYCLE-009
static void test_transition_hw_configured_to_rcp_configured_fails_when_implausible(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1] = { { true, true, true, false } };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    snap.endpoints      = eps;
    snap.endpoint_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root, true));
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
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, stranger, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, discovery, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

/* RESOLVED 2026-08-14 (REQ-LIFECYCLE-031, issue #341 lineage): TC18
 * §12.3.1.2's own "any valid stream_id/byte_bus_id combination" case --
 * distinct from via_discovery_stream/via_root_client_ep0, both already
 * covered above -- now authorizes this advance too. writer.
 * via_valid_stream_association is a caller-computed input here
 * (rcp_regmap_writer_ctx() is what actually derives it, including baking
 * in TC18's own "only when no root client is configured" narrowing --
 * see test_regmap.c's own writer_ctx tests for that derivation); this
 * function itself just has to honor it once set. */
static void test_transition_hw_configured_to_rcp_configured_accepts_valid_stream_association(void)
{
    rcp_lifecycle_endpoint_plausibility_t ep;
    rcp_lifecycle_request_stream_plausibility_t rs;
    rcp_lifecycle_plausibility_snapshot_t snap = plausible_snapshot(&ep, &rs);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t valid_assoc = {false, false, false, false, true};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, valid_assoc, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);
}

//cfusa:test REQ-LIFECYCLE-010
static void test_transition_hw_configured_to_hw_unconfigured_is_unconditional_once_authorized(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger = {0};
    rcp_lifecycle_writer_ctx_t root     = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, stranger, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

/* MC/DC closure: "target == HW_UNCONFIGURED && from == RCP_CONFIGURED"
 * (the RCP_CONFIGURED -> HW_UNCONFIGURED demotion gate) only ever
 * reaches this line with from==RCP_CONFIGURED true -- by elimination,
 * genuinely so: from==target is caught by this function's own no-op
 * shortcut before this line is ever reached (line 149), and
 * from==HW_CONFIGURED is caught by the sibling check just above.
 * rcp_lifecycle_state_t has exactly three real values, so once those
 * two are excluded, from can ONLY be RCP_CONFIGURED whenever this line
 * actually runs -- the second condition's own independent effect is
 * genuinely unreachable through any real state value. The only way to
 * demonstrate it is a corrupted/out-of-range *state -- itself a
 * meaningful defensive case, matching this project's own established
 * (rcp_pwrmode_t)99 idiom (tests/test_power.c) for the identical
 * situation in a sibling module. */
//cfusa:test REQ-LIFECYCLE-012
static void test_transition_from_corrupted_state_to_hw_unconfigured_is_rejected(void)
{
    rcp_lifecycle_state_t state = (rcp_lifecycle_state_t)0x99;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL((rcp_lifecycle_state_t)0x99, state); /* unchanged */
}

/* Same closed gap as the advance above, but for the mirror-image
 * HW_CONFIGURED -> HW_UNCONFIGURED reset -- TC18 §12.3.1.2's own
 * wording repeats the identical "discovery stream or a valid
 * stream_id/byte_bus_id combination" authorization for this direction
 * too (this function's own header doc comment quotes both sentences). */
static void test_transition_hw_configured_to_hw_unconfigured_accepts_valid_stream_association(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t valid_assoc = {false, false, false, false, true};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, valid_assoc, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

/* REQ-LIFECYCLE-037 (TC18 §12.7.4): "Changes in configuration via a
 * discovery request are no longer allowed" once RCP_CONFIGURED. Unlike
 * the HW_CONFIGURED->HW_UNCONFIGURED reset (still HW_CONFIGURED at the
 * time of the request, where the discovery stream remains a valid
 * authorizer -- see the next test), a demotion FROM RCP_CONFIGURED
 * requires writer.via_root_client_ep0 specifically; via_discovery_stream
 * alone -- previously sufficient, the REQ-LIFECYCLE-037 gap this test
 * used to pin -- is now rejected with RCP_LIFECYCLE_ERR_UNAUTHORIZED. */
//cfusa:test REQ-LIFECYCLE-011
static void test_transition_rcp_configured_to_hw_unconfigured_requires_root_client(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger    = {0};
    rcp_lifecycle_writer_ctx_t discovery   = {false, false, false, true};
    rcp_lifecycle_writer_ctx_t valid_assoc = {false, false, false, false, true};
    rcp_lifecycle_writer_ctx_t root        = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, stranger, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, discovery, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged -- discovery stream no
                                                                longer suffices once RCP_CONFIGURED */

    /* REQ-LIFECYCLE-031's own new via_valid_stream_association member
     * does NOT widen this specific reset -- §12.7.4's narrower rule
     * ("only... the root client") governs here, not §12.3.1.2's own
     * wider "any valid stream_id/byte_bus_id combination" case this
     * function only honors for the two other transitions above. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, valid_assoc, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

//cfusa:test REQ-LIFECYCLE-012
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
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

/* ADDED (issue #455): TC18 Figure 17 explicitly diagrams a
 * RCP_CONFIGURED -> HW_CONFIGURED arrow -- "Root Client or (stream/bb_ID
 * & no root configured) access via EP0 to set state to HW_CONFIGURED &
 * all other EPs are Idle -> send positive response" -- that
 * rcp_lifecycle_transition() previously did not implement at all,
 * falling through unconditionally to RCP_LIFECYCLE_ERR_INVALID_
 * TRANSITION regardless of writer or idleness. Root client via EP0,
 * idle, succeeds. */
//cfusa:test REQ-LIFECYCLE-039
static void test_transition_rcp_configured_to_hw_configured_succeeds_for_root_client(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

/* The label's own "(stream/bb_ID & no root configured)" alternative --
 * writer.via_valid_stream_association, which already bakes in "no root
 * client configured" at its own construction site -- also authorizes
 * this transition, same as the HW_CONFIGURED -> RCP_CONFIGURED advance's
 * own equivalent test. via_discovery_stream alone does NOT (unlike the
 * HW_CONFIGURED -> RCP_CONFIGURED advance): Figure 17's own label for
 * this arrow names only EP0-routed access, consistent with
 * REQ-LIFECYCLE-037's finding that the discovery stream no longer
 * authorizes a configuration change once already RCP_CONFIGURED. */
//cfusa:test REQ-LIFECYCLE-039
static void test_transition_rcp_configured_to_hw_configured_requires_correct_gate(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger    = {0};
    rcp_lifecycle_writer_ctx_t discovery   = {false, false, false, true};
    rcp_lifecycle_writer_ctx_t valid_assoc = {false, false, false, false, true};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, stranger, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, discovery, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged -- discovery
                                                                stream alone does not
                                                                authorize this arrow */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, valid_assoc, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

/* REQ-LIFECYCLE-022, TC18 Figure 17: "...& all other EPs are Idle ->
 * send positive response" -- an authorized (root client) request is
 * still refused with RCP_LIFECYCLE_ERR_EPS_NOT_IDLE, checked after
 * writer authorization, when all_other_eps_idle is false. This is
 * REQ-LIFECYCLE-022's real home for this label (issue #455 corrected
 * its prior misattribution to the HW_UNCONFIGURED -> HW_CONFIGURED
 * advance, see that transition's own test above). */
//cfusa:test REQ-LIFECYCLE-039
static void test_transition_rcp_configured_to_hw_configured_requires_idle(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_EPS_NOT_IDLE,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, root, false));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

//cfusa:test REQ-LIFECYCLE-013
static void test_transition_same_state_is_noop_success(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for a no-op */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                       rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_CONFIGURED, NULL, none, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, state);
}

/* ── Per-state request filtering ───────────────────────────────────────────── */

/* As of the REQ-LIFECYCLE-033 fix, rcp_lifecycle_should_accept() returns
 * a three-way rcp_lifecycle_accept_t (ACCEPT/DROP/REJECT), not a bool --
 * every call site below now asserts the specific value rather than
 * relying on implicit truthiness (RCP_LIFECYCLE_ACCEPT == 0 would
 * otherwise read as C-false, inverting every TEST_ASSERT_TRUE/FALSE
 * call that predates this fix). */
//cfusa:test REQ-LIFECYCLE-014
static void test_hw_unconfigured_accepts_discovery_abb_under_ntscf(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* MC/DC closure: "avtp_subtype != RCP_AVTP_SUBTYPE_NTSCF ||
 * byte_bus_id != RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID" only ever
 * reaches this line with avtp_subtype==NTSCF in every existing
 * HW_UNCONFIGURED test (RCP_AVTP_SUBTYPE_TSCF is dropped earlier, one
 * line up, before this check is ever reached) -- so the first
 * condition's own independent effect was never demonstrated. A third,
 * unrecognized AVTP subtype (neither NTSCF nor TSCF) while
 * HW_UNCONFIGURED must be dropped outright, matching every other
 * addressed-wrong case here. */
//cfusa:test REQ-LIFECYCLE-015
static void test_hw_unconfigured_drops_unrecognized_avtp_subtype(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        (uint8_t)0x7Fu, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

//cfusa:test REQ-LIFECYCLE-015
static void test_hw_unconfigured_drops_wrong_byte_bus_id(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)1u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* REQ-LIFECYCLE-033: a non-ABB message type addressed correctly to EP0
 * is REJECTed (TC18 §12.7's REQUEST_REJECTED), not silently DROPped as
 * this test's own name still says -- kept (not renamed) as the direct
 * predecessor of test_hw_unconfigured_rejects_non_abb_message_type_
 * addressed_to_ep0 below, which pins the corrected behavior explicitly;
 * this one now pins the wrong-byte-bus_id-AND-non-ABB combination, which
 * is still a DROP (byte_bus_id mismatch checked first). */
//cfusa:test REQ-LIFECYCLE-015
static void test_hw_unconfigured_drops_non_abb_message_type(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, (rcp_byte_bus_id_t)1u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* REQ-LIFECYCLE-033 (TC18 §12.7): a GBB-framed request correctly
 * addressed to EP0 on the discovery byte_bus_id while HW_UNCONFIGURED is
 * REJECTed with an error response, not silently dropped. */
//cfusa:test REQ-LIFECYCLE-015
static void test_hw_unconfigured_rejects_non_abb_message_type_addressed_to_ep0(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_REJECT, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

//cfusa:test REQ-LIFECYCLE-016
static void test_hw_unconfigured_drops_tscf_even_with_time_sync_supported(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

//cfusa:test REQ-LIFECYCLE-016
static void test_hw_unconfigured_drops_tscf_without_time_sync(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

static void test_hw_configured_applies_ordinary_tscf_drop_rule(void)
{
    /* Time-sync not supported -- TSCF still dropped, matching
     * rcp_avtp_should_drop_tscf()'s own general rule. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u, RCP_AVTP_TSCF_FALLBACK_DROP));
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
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* REQ-LIFECYCLE-033: a GBB-framed request addressed to EP0 while
 * HW_CONFIGURED is also REJECTed, not accepted unconditionally as the
 * pre-fix "unrestricted beyond byte_bus_id" rule read. */
static void test_hw_configured_rejects_non_abb_message_type_addressed_to_ep0(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_REJECT, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

static void test_rcp_configured_accepts_ntscf_at_any_byte_bus_id(void)
{
    /* Frame-level acceptance beyond the TSCF/time-sync rule is unrestricted
     * at this milestone; register-level write locking is a separate,
     * directly-tested concern (rcp_lifecycle_field_writable()). */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, (rcp_byte_bus_id_t)42u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* REQ-AVTP-029, TC18 §13.3's own configurable alternative to the
 * general drop rule test_hw_configured_applies_ordinary_tscf_drop_rule()
 * above already pins for RCP_AVTP_TSCF_FALLBACK_DROP: the SAME inputs
 * (RCP_CONFIGURED, time_sync_supported=false, TSCF) under
 * RCP_AVTP_TSCF_FALLBACK_IGNORE are no longer dropped -- RCP_CONFIGURED
 * has no OTHER unconditional TSCF-drop rule of its own (unlike
 * HW_UNCONFIGURED/HW_CONFIGURED, both proven elsewhere in this file to
 * still drop TSCF under IGNORE too -- see rcp_avtp_should_drop_tscf()'s
 * own doc comment, avtp.h, for why this rule's own config only ever
 * governs the general time-sync check, never those states' own separate,
 * always-unconditional rules), so ACCEPT is this rule's own directly
 * observable effect. */
//cfusa:test REQ-AVTP-029
//cfusa:test REQ-LIFECYCLE-017
static void test_rcp_configured_accepts_tscf_without_time_sync_when_policy_is_ignore(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u,
        RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* HW_UNCONFIGURED's own unconditional TSCF-drop rule (independent of
 * time_sync_supported, and of this policy) still applies -- proving
 * RCP_AVTP_TSCF_FALLBACK_IGNORE only ever suppresses the general
 * time-sync check at the top of rcp_lifecycle_should_accept(), never the
 * separate, state-specific rules below it. */
//cfusa:test REQ-LIFECYCLE-016
static void test_hw_unconfigured_still_drops_tscf_when_policy_is_ignore(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID,
        RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* Same reasoning as test_hw_unconfigured_still_drops_tscf_when_policy_is_
 * ignore() above, for HW_CONFIGURED's own separate unconditional rule. */
static void test_hw_configured_still_drops_tscf_when_policy_is_ignore(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(
        RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u,
        RCP_AVTP_TSCF_FALLBACK_IGNORE));
}

/* ── Register-locking-by-state ─────────────────────────────────────────────── */

/* As of the REQ-LIFECYCLE-026/035 fix: HW_GENERIC's HW_UNCONFIGURED
 * writability now requires writer.via_discovery_stream (TC18
 * §12.3.1.1/§12.7.2 -- "All configurations must be run via the stream
 * which was used for discovery" / "only configuration via the discovery
 * stream assigned due to a discovery request is feasible"). none (no
 * discovery-stream bit set) is no longer writable there; only a writer
 * that is the discovery claimant is. */
//cfusa:test REQ-LIFECYCLE-018
//cfusa:sec-test REQ-LIFECYCLE-018
static void test_hw_generic_writable_only_in_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t none      = {0};
    rcp_lifecycle_writer_ctx_t root      = { true, false };
    rcp_lifecycle_writer_ctx_t discovery = { false, false, false, true };

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, none));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, discovery));
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
//cfusa:test REQ-LIFECYCLE-019
//cfusa:sec-test REQ-LIFECYCLE-019
static void test_functional_w_not_writable_in_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, none));
}

//cfusa:test REQ-LIFECYCLE-019
//cfusa:sec-test REQ-LIFECYCLE-019
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

//cfusa:test REQ-LIFECYCLE-019
//cfusa:sec-test REQ-LIFECYCLE-019
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

//cfusa:test REQ-LIFECYCLE-020
//cfusa:sec-test REQ-LIFECYCLE-020
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
 * distinguishes a state-only denial (RCP_ERROR_LOCKED_MEM_ACCESS -- TC18
 * Figure 17's own "...configuration to HW_CONFIG or QUEUE_CFG or
 * EP_GEN_CFG -> send error response LOCKED_CONFIG_ACCESS") from a
 * writer/frame-only denial on top of an otherwise-permitting state
 * (RCP_ERROR_UNAUTHORIZED_ACCESS -- TC18 §13.7.1.2's own separate "write
 * prohibited register (e.g. lock bit for map set)" example), and
 * RCP_ERROR_NONE when writable. */
static void test_field_write_error_distinguishes_state_from_writer_denial(void)
{
    rcp_lifecycle_writer_ctx_t root           = { true, false, false, false };
    rcp_lifecycle_writer_ctx_t discovery      = { false, false, false, true };
    rcp_lifecycle_writer_ctx_t stranger        = { false, false, false, false };
    rcp_lifecycle_writer_ctx_t root_multicast  = { true, false, true, false };

    /* Writable: RCP_ERROR_NONE. HW_GENERIC in HW_UNCONFIGURED requires
     * writer.via_discovery_stream as of the REQ-LIFECYCLE-026/035 fix --
     * root alone (no root client can exist yet this early in bring-up) no
     * longer suffices here. */
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, discovery));

    /* State-only denial: even root can't write HW_GENERIC once past
     * HW_UNCONFIGURED -- LOCKED_MEM_ACCESS (Figure 17's
     * LOCKED_CONFIG_ACCESS), not UNAUTHORIZED_ACCESS, since HW_GENERIC's
     * own writability rule has no authorization concept at all. */
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));

    /* Same state-only reasoning applies to FUNCTIONAL_W_STAR once
     * RCP_CONFIGURED -- permanently locked for any writer. */
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, root));

    /* Writer-only denial: RCP_CONFIGURED would permit FUNCTIONAL_W for an
     * authorized writer, but stranger isn't one -- UNAUTHORIZED_ACCESS. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));

    /* Non-unicast-only denial: root would otherwise be authorized --
     * UNAUTHORIZED_ACCESS. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root_multicast));
}

/* REQ-RMAP-025 (TC18 §12.7.5 Table 18, access type "R"): the RC Server
 * general (static) register map is never writable, in any state, by any
 * writer -- unlike every other field kind, no writer condition (root
 * client, owning stream, discovery stream, even a maximally-privileged
 * combination of all three) can make it writable. Proven across all
 * three lifecycle states with both an unprivileged and a fully-
 * privileged writer, and that the denial reports LOCKED_MEM_ACCESS (a
 * pure state-driven lock, matching HW_GENERIC's and FUNCTIONAL_W_STAR's
 * own reasoning above), never UNAUTHORIZED_ACCESS -- no writer could
 * ever fix this denial, so it is never framed as an authorization
 * failure. */
static void test_read_only_never_writable_in_any_state_by_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t stranger = {false, false, false, false};
    rcp_lifecycle_writer_ctx_t everything = {true, true, false, true}; /* every
                                                                           authorizing
                                                                           condition
                                                                           true,
                                                                           unicast */

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_READ_ONLY, everything));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_READ_ONLY, everything));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_READ_ONLY, everything));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_READ_ONLY, stranger));

    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_READ_ONLY, everything));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

//cfusa:test REQ-LIFECYCLE-021
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
    RUN_TEST(test_rcp_cfg_inconsistent_endpoint_lacks_stream_assoc_despite_matching_index);
    RUN_TEST(test_rcp_cfg_null_snapshot_is_inconsistent);

    RUN_TEST(test_transition_hw_unconfigured_to_hw_configured_succeeds_when_plausible);
    RUN_TEST(test_transition_hw_unconfigured_to_hw_configured_not_idle_gated);
    RUN_TEST(test_transition_hw_unconfigured_to_hw_configured_fails_when_implausible);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_succeeds_when_plausible);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_fails_when_implausible);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_requires_authorization);
    RUN_TEST(test_transition_hw_configured_to_rcp_configured_accepts_valid_stream_association);
    RUN_TEST(test_transition_from_corrupted_state_to_hw_unconfigured_is_rejected);
    RUN_TEST(test_transition_hw_configured_to_hw_unconfigured_is_unconditional_once_authorized);
    RUN_TEST(test_transition_hw_configured_to_hw_unconfigured_accepts_valid_stream_association);
    RUN_TEST(test_transition_rcp_configured_to_hw_unconfigured_requires_root_client);
    RUN_TEST(test_transition_rejects_skipping_hw_configured);
    RUN_TEST(test_transition_rcp_configured_to_hw_configured_succeeds_for_root_client);
    RUN_TEST(test_transition_rcp_configured_to_hw_configured_requires_correct_gate);
    RUN_TEST(test_transition_rcp_configured_to_hw_configured_requires_idle);
    RUN_TEST(test_transition_same_state_is_noop_success);

    RUN_TEST(test_hw_unconfigured_accepts_discovery_abb_under_ntscf);
    RUN_TEST(test_hw_unconfigured_drops_unrecognized_avtp_subtype);
    RUN_TEST(test_hw_unconfigured_drops_wrong_byte_bus_id);
    RUN_TEST(test_hw_unconfigured_drops_non_abb_message_type);
    RUN_TEST(test_hw_unconfigured_rejects_non_abb_message_type_addressed_to_ep0);
    RUN_TEST(test_hw_unconfigured_drops_tscf_even_with_time_sync_supported);
    RUN_TEST(test_hw_unconfigured_drops_tscf_without_time_sync);
    RUN_TEST(test_hw_configured_applies_ordinary_tscf_drop_rule);
    RUN_TEST(test_hw_configured_drops_tscf_even_when_time_sync_supported);
    RUN_TEST(test_hw_configured_rejects_non_abb_message_type_addressed_to_ep0);
    RUN_TEST(test_rcp_configured_accepts_ntscf_at_any_byte_bus_id);
    RUN_TEST(test_rcp_configured_accepts_tscf_without_time_sync_when_policy_is_ignore);
    RUN_TEST(test_hw_unconfigured_still_drops_tscf_when_policy_is_ignore);
    RUN_TEST(test_hw_configured_still_drops_tscf_when_policy_is_ignore);

    RUN_TEST(test_hw_generic_writable_only_in_hw_unconfigured);
    RUN_TEST(test_functional_w_not_writable_in_hw_unconfigured);
    RUN_TEST(test_functional_w_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_w_requires_authorized_writer_once_rcp_configured);
    RUN_TEST(test_functional_w_star_permanently_locked_once_rcp_configured);
    RUN_TEST(test_field_writable_denies_non_unicast_frame_regardless_of_kind_or_authorization);
    RUN_TEST(test_field_write_error_distinguishes_state_from_writer_denial);
    RUN_TEST(test_read_only_never_writable_in_any_state_by_any_writer);

    RUN_TEST(test_lifecycle_strerror_unique_nonempty);

    return UNITY_END();
}
