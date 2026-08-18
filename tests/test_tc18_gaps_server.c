/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LIFECYCLE-022
//cfusa:test REQ-LIFECYCLE-023
//cfusa:test REQ-LIFECYCLE-024
//cfusa:test REQ-LIFECYCLE-025
//cfusa:test REQ-LIFECYCLE-026
//cfusa:test REQ-LIFECYCLE-027
//cfusa:test REQ-LIFECYCLE-028
//cfusa:test REQ-LIFECYCLE-029
//cfusa:test REQ-LIFECYCLE-030
//cfusa:test REQ-LIFECYCLE-031
//cfusa:test REQ-LIFECYCLE-032
//cfusa:test REQ-LIFECYCLE-033
//cfusa:test REQ-LIFECYCLE-034
//cfusa:test REQ-LIFECYCLE-035
//cfusa:test REQ-LIFECYCLE-036
//cfusa:test REQ-LIFECYCLE-037
//cfusa:test REQ-LIFECYCLE-038
//cfusa:test REQ-PWRMODE-014
//cfusa:test REQ-PWRMODE-015
//cfusa:test REQ-PWRMODE-016
//cfusa:test REQ-PWRMODE-017
//cfusa:test REQ-PWRMODE-018
//cfusa:test REQ-PWRMODE-019
//cfusa:test REQ-PWRMODE-020
//cfusa:test REQ-PWRMODE-021
//cfusa:test REQ-PWRMODE-022
//cfusa:test REQ-PWRMODE-023
//cfusa:test REQ-PWRMODE-024
//cfusa:test REQ-PWRMODE-025
//cfusa:test REQ-PWRMODE-026
//cfusa:test REQ-PWRMODE-027
//cfusa:test REQ-PWRMODE-028
//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
//cfusa:test REQ-SRV-017
//cfusa:test REQ-SRV-018
//cfusa:test REQ-SEQ-012
//cfusa:test REQ-SEQ-013
//cfusa:test REQ-SEQ-014
//cfusa:test REQ-TIMED-012
//cfusa:test REQ-TIMED-013
//cfusa:test REQ-WDG-010

/*
 * test_tc18_gaps_server.c -- spec-literal conformance-and-deviation suite
 * for the RC-Server-side TC18 clauses catalogued by the v0.105.0
 * requirements-corpus completeness pass: RC Server lifecycle (§12.3,
 * §12.7), power/operation modes and the Goto Sleep / Goto StandBy
 * exchange (§12.4-§12.5, §13.7.2.3), request handling on enabled and
 * disabled endpoints (§12.3.1.3, §13.7.1), sequencers (§12.7.10 Table 28),
 * the per-stream request watchdog (§12.7.7), and TSCF-carried timed
 * requests (§11.2).
 *
 * Every requirement catalogued by that pass with a status of "partial" or
 * "not-implemented" gets a *deviation-pinning* test here: the assertions
 * state what this library actually does today, and the comment above each
 * one names the TC18 clause that is therefore not met and what a
 * conforming RC Server would do instead. These tests are expected to be
 * rewritten -- not merely re-run -- when the corresponding gap is closed.
 */
#include "unity.h"

#include "../src/mem_bounded.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/discovery.h>
#include <rcp/ep_wakeup.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/power.h>
#include <rcp/powerstate.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/request_compound.h>
#include <rcp/request_sequencer.h>
#include <rcp/request_timed.h>
#include <rcp/request_triggered.h>
#include <rcp/server.h>
#include <rcp/watchdog.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Shared fixtures ───────────────────────────────────────────────────────── */

static const uint8_t MAC_A[6] = {0x02u, 0u, 0u, 0u, 0u, 0x0Au};
static const uint8_t MAC_B[6] = {0x02u, 0u, 0u, 0u, 0u, 0x0Bu};

static rcp_avtp_addr_t addr_of(const uint8_t mac[6], uint16_t uid, rcp_byte_bus_id_t bus)
{
    rcp_avtp_addr_t a;

    a.stream_id   = rcp_stream_id_make(mac, uid);
    a.byte_bus_id = bus;
    return a;
}

/* A plain, unconditional ACF_ABB standard request addressed to bus. */
static rcp_bytes_t standard_abb(rcp_byte_bus_id_t bus, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               payload[2] = {0xDEu, 0xADu};

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id     = bus;
    hdr.transaction_num = transaction_num;
    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

/* standard_abb()'s own evt-controllable sibling (issue #463): that helper
 * hardcodes evt=0 (no acknowledge requested), which cannot exercise
 * rcp_server_endpoint_admit_with_ack()'s own evt[3] "if requested" gate. */
static rcp_bytes_t standard_abb_with_evt(rcp_byte_bus_id_t bus, uint8_t transaction_num,
                                          uint8_t evt)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               payload[2] = {0xDEu, 0xADu};

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id     = bus;
    hdr.transaction_num = transaction_num;
    hdr.evt             = evt;
    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

/* ── §12.3 lifecycle transitions: idleness and writer authorization both now enforced ── */

/* As of the REQ-LIFECYCLE-022 fix, TC18 Figure 17's "Root Client access
 * via EP0 to set state to HW_UNCONFIGURED & other EPs are not Idle ->
 * send error response EPs_NOT_IDLE" transition is enforced directly:
 * rcp_lifecycle_transition() now takes an all_other_eps_idle input and
 * refuses an otherwise-authorized demotion while it is false, rather
 * than tearing down in-flight requests unconditionally. */
static void test_transition_now_requires_idle_for_demotion(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_EPS_NOT_IDLE,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, false));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);
}

/* As of the REQ-LIFECYCLE-031 fix, rcp_lifecycle_transition() does take a
 * writer context, and does reject an anonymous/unauthorized caller's
 * demotion request -- the second half of this test's original title no
 * longer describes a gap, so RCP_LIFECYCLE_ERR_UNAUTHORIZED's own
 * strerror() case is asserted directly instead of a still-unknown-error-
 * code probe (rcp_lifecycle_errc_t now covers 0-4, not 0-3). */
static void test_transition_now_rejects_unauthorized_writer(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    rcp_lifecycle_writer_ctx_t stranger = {0};
    const char                *unknown;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, stranger, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */
    TEST_ASSERT_TRUE(strlen(rcp_lifecycle_strerror(RCP_LIFECYCLE_ERR_UNAUTHORIZED)) > 0);

    /* A truly unknown code (rcp_lifecycle_errc_t now covers 0-4) still
     * falls through to the generic "unknown error" message. */
    unknown = rcp_lifecycle_strerror((rcp_lifecycle_errc_t)99);
    TEST_ASSERT_TRUE(strcmp(unknown, rcp_lifecycle_strerror(RCP_LIFECYCLE_OK)) != 0);
    TEST_ASSERT_TRUE(strcmp(unknown,
                            rcp_lifecycle_strerror(RCP_LIFECYCLE_ERR_INVALID_TRANSITION)) != 0);
    TEST_ASSERT_TRUE(strcmp(unknown,
                            rcp_lifecycle_strerror(RCP_LIFECYCLE_ERR_UNAUTHORIZED)) != 0);
}

/* ── §12.3 register locking: field kinds and error response, both closed ───── */

/* As of the REQ-LIFECYCLE-023 fix: rcp_lifecycle_field_kind_t's own doc
 * comment now explicitly documents that RCP_LIFECYCLE_FIELD_HW_GENERIC
 * covers the endpoint-generic (rcp_regmap_ep_generic_cfg_t) and
 * response-queue/request-stream (rcp_regmap_response_queue_cfg_t /
 * rcp_regmap_request_stream_cfg_t, where not already covered by Table
 * 22's own separate FUNCTIONAL_W_STAR legend) blocks too -- TC18 Figure
 * 16 groups all of HW_CONFIG/QUEUE_CFG/EP_GEN_CFG under one identical
 * locking rule and one identical LOCKED_CONFIG_ACCESS response, and
 * HW_GENERIC's own pre-existing writability rule (writable only in
 * HW_UNCONFIGURED, for any writer) is already exactly that rule -- no
 * new enum value or behavior change was needed, only recognizing that
 * this kind's existing scope was under-documented, not that it was
 * incomplete. The out-of-range placeholder kind this test previously
 * used to demonstrate the gap is gone; HW_GENERIC itself now stands in
 * directly for all three blocks. As of the REQ-LIFECYCLE-024/
 * REQ-WIREERR-004 fix (corrected once already, see that function's own
 * header doc comment): rcp_lifecycle_field_write_error() answers this
 * exact denial with RCP_ERROR_LOCKED_MEM_ACCESS, the closest numbered
 * wire code to Figure 17's own diagram-only "LOCKED_CONFIG_ACCESS"
 * name. */
static void test_hw_generic_covers_ep_generic_and_queue_config_with_locked_response(void)
{
    rcp_lifecycle_writer_ctx_t root      = {true, true};
    /* HW_UNCONFIGURED writability requires the discovery claimant
     * specifically (REQ-LIFECYCLE-026/035) -- no root client can exist
     * yet this early in bring-up. */
    rcp_lifecycle_writer_ctx_t discovery = {false, false, false, true};

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, discovery));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, root));

    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
    TEST_ASSERT_EQUAL_INT(4, RCP_ERROR_LOCKED_MEM_ACCESS);
    TEST_ASSERT_TRUE(strlen(rcp_wire_error_string(RCP_ERROR_LOCKED_MEM_ACCESS)) > 0);
}

/* ── §12.3.1.2 / §12.7: unknown streams and non-EP0 endpoints ──────────────── */

/* As of the REQ-LIFECYCLE-032 fix, HW_CONFIGURED admits only EP0
 * (RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID) -- byte_bus_id 42 is now
 * correctly rejected there. RCP_CONFIGURED remains unrestricted at the
 * byte_bus_id level (REQ-LIFECYCLE-025/036 -- unknown stream_id/
 * byte_bus_id combinations and writer authorization -- remain real,
 * separate gaps: rcp_lifecycle_should_accept() still receives no
 * stream_id and consults no association table or writer identity at
 * all, so an endpoint no configured request stream maps is still
 * admitted once RCP_CONFIGURED). */
static void test_hw_configured_admits_only_ep0(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)42u, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)42u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* DEVIATION PIN (REQ-LIFECYCLE-025/036, not implemented): the admission
 * pipeline behind rcp_lifecycle_should_accept() -- specifically
 * rcp_server_endpoint_admit(), called directly here, bypassing
 * should_accept() the same way a real caller would only after
 * should_accept() has already let a frame through -- takes no lifecycle
 * state, no stream_id, and no writer identity at all, so an operational
 * request executes immediately regardless of whether the endpoint it
 * targets has any configured stream association or who sent it. */
static void test_admit_takes_no_lifecycle_state_or_stream_identity(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)42u, 1u);
    uint8_t               request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* FIXED 2026-08-12 (issue #201, REQ-LIFECYCLE-038): TC18 §12.3.1.2's
 * RCP_CFG_INCONSISTENT plausibility check names three bullets;
 * rcp_lifecycle_check_rcp_cfg() now implements all three (has_stream_assoc
 * per used endpoint, has_response_stream per configured request stream,
 * and -- as of this fix -- at least one endpoint's own request_stream_index
 * referencing every configured request stream, so no stream is left
 * orphaned with zero endpoints actually using it). This test used to pin
 * the deviation (a snapshot with zero endpoints and one "configured"
 * stream passed as plausible); it now asserts the conforming rejection,
 * per this file's own documented convention that a gap-pinning test
 * failing after a fix means "rewrite it to the conforming expectation." */
static void test_rcp_cfg_inconsistent_catches_an_orphaned_stream(void)
{
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 0 }, /* configured, has_response_stream, response_stream_index -- all "satisfied" */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    /* No endpoints at all reference this stream -- TC18's own bullet 2
     * now correctly rejects this as RCP_CFG_INCONSISTENT. response_stream_
     * count is set so streams[0]'s own REQ-RMAP-049 check passes too --
     * this test isolates the orphan bullet specifically, not a
     * response_stream_index-out-of-range rejection getting there first. */
    snap.endpoints             = NULL;
    snap.endpoint_count        = 0;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* The positive case: an endpoint whose own request_stream_index correctly
 * references the configured stream satisfies bullet 2, and the whole
 * snapshot is plausible (matching test_rcp_cfg_consistent_when_satisfied's
 * own single-stream shape, but exercising this specific field to prove
 * it is actually consulted, not merely present). */
static void test_rcp_cfg_inconsistent_a_bound_endpoint_satisfies_bullet_two(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 0 },
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = true;
    eps[0].hw_pin_mapped        = true;
    eps[0].has_request_stream   = true;
    eps[0].has_stream_assoc     = true;
    eps[0].request_stream_index = 0;

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1; /* REQ-RMAP-049: streams[0]'s own response_stream_index (0) is valid */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* Two configured streams; only one has a bound endpoint. The unbound
 * stream is still caught, even in the presence of an otherwise-plausible
 * sibling stream -- bullet 2 is evaluated per-stream, not in aggregate. */
static void test_rcp_cfg_inconsistent_catches_the_specific_orphaned_stream_among_several(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[2] = {
        { true, true, 0 }, /* stream 0: bound below */
        { true, true, 0 }, /* stream 1: configured, but nothing references it */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = true;
    eps[0].hw_pin_mapped        = true;
    eps[0].has_request_stream   = true;
    eps[0].has_stream_assoc     = true;
    eps[0].request_stream_index = 0;

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 2;
    snap.response_stream_count = 1; /* REQ-RMAP-049: both streams' own response_stream_index (0) is
                                        valid -- isolates the orphan bullet this test targets */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* REQ-RMAP-049 (issue #338): has_response_stream alone is not enough --
 * a stream whose own response_stream_index doesn't name a real slot in
 * snap->own response_stream_count space is still RCP_CFG_INCONSISTENT,
 * even though bullet 2's old, weaker check (has_response_stream == true)
 * would have accepted it. Isolates this specific rejection from the
 * orphan-stream bullet above (this endpoint IS bound to the stream --
 * request_stream_index correctly references it -- so only the new
 * response_stream_index range check can be what fails here). */
static void test_rcp_cfg_inconsistent_response_stream_index_out_of_range(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 1 }, /* response_stream_index 1 -- but response_stream_count is only 1 below,
                               so the only real slot is index 0; 1 names nothing */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = true;
    eps[0].hw_pin_mapped        = true;
    eps[0].has_request_stream   = true;
    eps[0].has_stream_assoc     = true;
    eps[0].request_stream_index = 0;

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* The mirror-image positive case: response_stream_count widened to 2
 * makes the identical streams[0].response_stream_index == 1 from the
 * test above a real, valid slot -- proving the check is a genuine range
 * comparison, not a disguised "must be 0" rule. */
static void test_rcp_cfg_consistent_response_stream_index_within_range(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 1 },
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = true;
    eps[0].hw_pin_mapped        = true;
    eps[0].has_request_stream   = true;
    eps[0].has_stream_assoc     = true;
    eps[0].request_stream_index = 0;

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 2;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* An endpoint with has_stream_assoc FALSE is never consulted for bullet
 * 2, regardless of what its own request_stream_index happens to hold --
 * that field is meaningless without has_stream_assoc, per its own doc
 * comment (lifecycle.h). Confirms this endpoint's own already-failing
 * bullet 1 (a used endpoint with no stream association) is what's
 * actually caught here, not a false-positive bullet-2 match. */
static void test_rcp_cfg_inconsistent_ignores_stream_index_without_stream_assoc(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true },
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = true;
    eps[0].hw_pin_mapped        = true;
    eps[0].has_request_stream   = true;
    eps[0].has_stream_assoc     = false; /* bullet 1 already fails on this */
    eps[0].request_stream_index = 0;     /* would satisfy bullet 2 if consulted */

    snap.endpoints            = eps;
    snap.endpoint_count       = 1;
    snap.request_streams      = streams;
    snap.request_stream_count = 1;

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* A stale/unused endpoint slot (ep_used == false) with leftover
 * has_stream_assoc == true and a matching request_stream_index must NOT
 * be able to "cover" an otherwise-orphaned stream for bullet 2 -- an
 * unused slot's own has_stream_assoc/request_stream_index values are
 * never validated by bullet 1 (its own loop skips unused endpoints
 * entirely via `continue`), so bullet 2 must not trust them either.
 * This is the discriminating case that isolates bullet 2's own ep_used
 * gate from bullet 1's own has_stream_assoc requirement -- unlike
 * test_rcp_cfg_inconsistent_ignores_stream_index_without_stream_assoc
 * above (ep_used == true, masked by bullet 1's own earlier check), this
 * endpoint passes bullet 1 trivially (skipped) and reaches bullet 2 with
 * has_stream_assoc == true, so only the ep_used check can catch it. */
static void test_rcp_cfg_inconsistent_an_unused_endpoint_does_not_cover_a_stream(void)
{
    rcp_lifecycle_endpoint_plausibility_t eps[1];
    rcp_lifecycle_request_stream_plausibility_t streams[1] = {
        { true, true, 0 }, /* configured, has_response_stream, response_stream_index */
    };
    rcp_lifecycle_plausibility_snapshot_t snap = {0};

    eps[0].ep_used              = false; /* unused -- skipped by bullet 1 entirely */
    eps[0].hw_pin_mapped        = false;
    eps[0].has_request_stream   = false;
    eps[0].has_stream_assoc     = true; /* stale leftover value on an unused slot */
    eps[0].request_stream_index = 0;    /* matches streams[0] -- must NOT count */

    snap.endpoints             = eps;
    snap.endpoint_count        = 1;
    snap.request_streams       = streams;
    snap.request_stream_count  = 1;
    snap.response_stream_count = 1; /* REQ-RMAP-049: streams[0]'s own response_stream_index (0) is
                                        valid -- isolates the ep_used-gate bullet this test targets */

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_RCP_CFG_INCONSISTENT, rcp_lifecycle_check_rcp_cfg(&snap));
}

/* ── §12.3.1.1 / §12.7.2 / §12.7: HW_UNCONFIGURED admission ────────────────── */

/* As of the REQ-LIFECYCLE-033 fix: the REQUEST_REJECTED half of what
 * this test originally pinned as a gap is now closed. The should_accept()
 * frame-admission half is NOT a gap after all, on closer reading: TC18's
 * "only configuration via the discovery stream" rule (§12.3.1.1/§12.7.2,
 * REQ-LIFECYCLE-026/035 -- closed) is a WRITE-authorization rule, and this
 * codebase's established layering puts write authorization at
 * rcp_lifecycle_field_writable(), not at should_accept()'s frame-level
 * admission -- the same layering distinction REQ-LIFECYCLE-026/035's own
 * batch corrected. should_accept() admitting B's frame on the same terms
 * as claimant A's is therefore expected, not a bug: admission is not
 * authorization. The full pipeline still correctly refuses B's actual
 * configuration WRITE, demonstrated below by composing the claim query
 * into a writer_ctx exactly as discovery.h's own file header describes. */
static void test_hw_unconfigured_admission_ignores_claimant_but_writes_still_gated(void)
{
    rcp_discovery_claim_t      claim;
    rcp_stream_id_t            a = rcp_stream_id_make(MAC_A, 1u);
    rcp_stream_id_t            b = rcp_stream_id_make(MAC_B, 2u);
    rcp_lifecycle_writer_ctx_t writer_a = {0};
    rcp_lifecycle_writer_ctx_t writer_b = {0};

    rcp_discovery_claim_init(&claim, 20u);
    rcp_discovery_claim_note_request(&claim, a, 1000u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1000u));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, b, 1000u));

    /* Frame-level admission does not consult stream identity -- correctly
     * so, since that is field_writable()'s job, not should_accept()'s. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));

    /* ...but once admitted, the actual write is gated correctly: A (the
     * claimant) may write HW_GENERIC, B may not (REQ-LIFECYCLE-026/035). */
    writer_a.via_discovery_stream = rcp_discovery_claim_is_claimant(&claim, a, 1000u);
    writer_b.via_discovery_stream = rcp_discovery_claim_is_claimant(&claim, b, 1000u);
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, writer_a));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, writer_b));

    /* TC18 §12.7 accepts only unconditional STANDARD requests at EP0 in this
     * condition and answers every other otherwise-valid EP0 request with
     * REQUEST_REJECTED (wire code 11, REQ-LIFECYCLE-033 -- closed). */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_REJECT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL_INT(11, RCP_ERROR_REQUEST_REJECTED);
}

/* ── §12.3.1.x / §12.7.3: who may write, and over what framing ─────────────── */

/* As of REQ-LIFECYCLE-027 (unicast) and REQ-LIFECYCLE-030/036
 * (authorization), both halves of what this test originally pinned as an
 * open gap are now closed -- renamed and rewritten to assert the
 * corrected behavior directly, rather than left stale documenting a bug
 * that no longer exists. */
static void test_hw_configured_write_access_now_requires_unicast_and_authorization(void)
{
    rcp_lifecycle_writer_ctx_t stranger    = {false, false, false, false};
    rcp_lifecycle_writer_ctx_t root        = {true, false, false, false};
    rcp_lifecycle_writer_ctx_t discovery   = {false, false, false, true};
    rcp_lifecycle_writer_ctx_t broadcast   = {true, false, true, false}; /* authorized
                                                                             root client,
                                                                             but non-unicast */

    /* TC18 §12.3.1.2 permits EP0 write access to another endpoint's
     * configuration only for the configured root client or via the discovery
     * stream, and §12.7.3 permits configuration access in HW_CONFIGURED only
     * over the discovery stream or a known stream_id/byte_bus_id pair. As of
     * REQ-LIFECYCLE-030/036, c-RCP now enforces exactly this: an unauthorized
     * "stranger" is refused, while the root client (via EP0) or the
     * discovery stream are each independently sufficient. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                   stranger));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, discovery));

    /* RCP_CONFIGURED's own authorization rule (pre-existing, unchanged by
     * this batch) is narrower: the discovery stream alone does not suffice
     * there (a distinct, still-open §12.7.4 concern -- see this file's own
     * deviation pin below), only root-client-via-EP0 or the owning stream. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root));

    /* TC18 §12.3.1.1, §12.3.1.2 and §12.3.1.3 each restate that a write is
     * accepted only from a unicast frame (REQ-LIFECYCLE-027) -- independent
     * of and composed with the authorization gate above: an otherwise-
     * authorized root-client writer is still refused once its frame is
     * flagged non-unicast. rcp_lifecycle_should_accept() (below) still
     * takes no destination-MAC input at all -- deliberately so: TC18's
     * unicast rule is scoped to write requests specifically, not general
     * frame admission, so should_accept's frame-level acceptance is
     * correctly unicast-agnostic; only a WRITE attempt is gated, and only
     * at the field_writable() layer where TC18 §12.7's write path already
     * lives. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                    RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, broadcast));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)9u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* ── §12.3.1.2: TSCF and ACF_GBB in HW_CONFIGURED ──────────────────────────── */

/* As of the REQ-LIFECYCLE-028 fix, TC18 §12.3.1.2's TSCF rule is enforced:
 * a TSCF-headed AVTPDU is dropped unconditionally in HW_CONFIGURED, the
 * same rule c-RCP already implemented for HW_UNCONFIGURED -- neither
 * state has the validated stream/byte_bus_id mapping and response queues
 * TSCF's presentation-time semantics presuppose. */
static void test_hw_configured_drops_tscf(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u, RCP_AVTP_TSCF_FALLBACK_DROP));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_DROP, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
    /* The TSCF rule does not apply once RCP_CONFIGURED -- the mapping it
     * guards against not existing yet has, by then, been validated. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ACCEPT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_RCP_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* As of the REQ-LIFECYCLE-033 fix: this test's own name is now the
 * opposite of what it asserts, kept (renamed) rather than deleted, since
 * it directly pins the correction. TC18 §12.3.1.2's "requests in
 * ACF_GBB format[are dropped]" (REQ-LIFECYCLE-029's own citation) and
 * §12.7's EP0-scoped REQUEST_REJECTED rule looked contradictory for this
 * exact case -- reconciled by treating §12.3.1.2 as the general,
 * non-EP0-scoped rule (byte_bus_id 7, this test's sibling above, is
 * dropped for that reason, unchanged) and §12.7 as the more specific,
 * EP0-scoped override that governs here. See rcp_lifecycle_should_accept()'s
 * own header doc comment for the full reconciliation. REQ-LIFECYCLE-029's
 * own `.fusa-reqs.json` text updated to record this resolution. */
static void test_hw_configured_rejects_gbb_addressed_to_ep0(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_REJECT, rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_TSCF_FALLBACK_DROP));
}

/* ── §12.7.4: discovery-stream write authority after RCP_CONFIGURED ────────── */

/* As of the REQ-LIFECYCLE-037 fix: the real write-authorization gates
 * (rcp_lifecycle_transition()'s RCP_CONFIGURED->HW_UNCONFIGURED demotion
 * and rcp_lifecycle_field_writable()'s FUNCTIONAL_W RCP_CONFIGURED
 * branch, the latter already correct before this fix) both now reject
 * bare writer.via_discovery_stream once RCP_CONFIGURED, per §12.7.4's
 * "Changes in configuration via a discovery request are no longer
 * allowed... only allowed via configured stream_id/byte_bus_id
 * combinations to the EP or via the root client." Still-honest residual,
 * not itself a conformance gap: rcp_discovery_claim_note_config_write()
 * (discovery.h) is a pure claim/timeout bookkeeping primitive that
 * deliberately does not consult lifecycle state at all -- see that
 * module's own file header, "this module deliberately does not itself
 * decide whether a given register field is writable... a caller
 * combines [its answer] with [rcp_lifecycle_field_writable()] as one
 * more input". It keeps refreshing the claim's own Discovery_TimeOut
 * regardless of lifecycle state, but that refreshed claim no longer
 * translates into actual write authority once RCP_CONFIGURED, since the
 * two real gates above now both close that door independently of
 * whether the claim primitive itself still says "held". */
static void test_discovery_write_authority_survives_rcp_configured(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t       a     = rcp_stream_id_make(MAC_A, 1u);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_endpoint_plausibility_t eps[1]     = {{true, true, true, true}};
    rcp_lifecycle_request_stream_plausibility_t rs[1] = {{true, true, 0}};
    rcp_lifecycle_plausibility_snapshot_t snap;
    rcp_lifecycle_writer_ctx_t discovery = {false, false, false, true};
    rcp_lifecycle_writer_ctx_t root      = {true, false, false, false};

    snap.endpoints             = eps;
    snap.endpoint_count        = 1u;
    snap.request_streams       = rs;
    snap.request_stream_count  = 1u;
    snap.response_stream_count = 1u; /* REQ-RMAP-049: rs[0]'s own response_stream_index (0) is valid */

    rcp_discovery_claim_init(&claim, 20u);
    rcp_discovery_claim_note_request(&claim, a, 1000u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_config_write(&claim, a, 1005u));

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, discovery, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);

    /* The claim primitive itself still refreshes/confirms for the
     * claimant, by design (it takes no lifecycle state -- see this
     * test's own header comment). */
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_config_write(&claim, a, 1010u));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1010u));

    /* ...but neither real gate honors that claim once RCP_CONFIGURED:
     * the demotion transition rejects a bare discovery writer... */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_UNAUTHORIZED,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, discovery, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state); /* unchanged */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);

    /* ...and FUNCTIONAL_W field writes reject it too. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, discovery));
}

/* ── §12.3 / §12.4.1: cold start recovers the configured lifecycle state ──── */

static void test_cold_start_target_recovers_the_configured_lifecycle_state(void)
{
    /* REQ-PWRMODE-014 (TC18 §12.3, §12.4.1): "After a cold start the RC
     * Server will be in its configured lifecycle state" -- recovered from
     * NVM, or from device defaults, which may themselves be an advanced
     * state. rcp_pwrmode_cold_start_lifecycle_target() takes that
     * already-recovered fact as a required parameter (this module owns no
     * NVM access of its own -- the same "caller supplies already-
     * classified inputs" convention as network_available) and returns it
     * unchanged, so a server that reached RCP_CONFIGURED before a power
     * cycle no longer silently loses it. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED,
                      rcp_pwrmode_cold_start_lifecycle_target(RCP_LIFECYCLE_RCP_CONFIGURED));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED,
                      rcp_pwrmode_cold_start_lifecycle_target(RCP_LIFECYCLE_HW_CONFIGURED));

    /* A device with nothing recovered (no NVM, no advanced default) still
     * gets this function's own long-standing safe default. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED,
                      rcp_pwrmode_cold_start_lifecycle_target(RCP_LIFECYCLE_HW_UNCONFIGURED));
}

/* ── §12.4: register-map config and wake sources survive StandBy ──────────── */

static void test_standby_is_classified_hot_so_configuration_is_retained(void)
{
    rcp_regmap_general_t           map;
    rcp_regmap_general_t           before;
    rcp_ep_wakeup_functional_cfg_t wake;
    rcp_ep_wakeup_functional_cfg_t wake_before;
    rcp_pwrmode_t                  mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t       kind = RCP_PWRMODE_START_COLD;

    rcp_regmap_general_init(&map);
    map.svr_ep_count = 4u;
    rcp_ep_wakeup_functional_cfg_init(&wake);
    wake.sources[0].enabled = true;
    rcp_memcpy_bounded(&before, sizeof(before), &map, sizeof(before));
    rcp_memcpy_bounded(&wake_before, sizeof(wake_before), &wake, sizeof(wake_before));

    /* REQ-PWRMODE-015 (TC18 §12.4, §12.4.1): "only a transition classified
     * RCP_PWRMODE_START_COLD may discard configuration." power.h owns no
     * regmap/wake-source storage of its own (regmap.c/ep_wakeup.c do) --
     * the guarantee this library actually provides, and the only one it
     * architecturally can, is that Normal<->StandBy is correctly and
     * always classified RCP_PWRMODE_START_HOT (this module's own doc
     * comment on RCP_PWRMODE_START_HOT now states the retention
     * obligation that classification carries explicitly), so an
     * integrator that correctly withholds re-init on HOT never has a
     * reason to touch either structure. Both are indeed byte-identical
     * below, by construction, not merely by omission. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_NORMAL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL_MEMORY(&before, &map, sizeof(map));
    TEST_ASSERT_EQUAL_MEMORY(&wake_before, &wake, sizeof(wake));
}

/* ── §12.4.1: the hot-start sequence's missing inputs ──────────────────────── */

/* As of the REQ-PWRMODE-016 fix (batch 3) and the REQ-PWRMODE-017 fix
 * (this batch), this test's own name is now the opposite of what it
 * asserts -- kept (renamed) rather than deleted, since it directly pins
 * both corrections together, matching how the two gaps were originally
 * discovered as one combined deviation pin. */
static void test_hotstart_now_has_network_check_and_records_responder_stream(void)
{
    rcp_pwrmode_handshake_t   hs;
    rcp_avtp_addr_t           sleeper = addr_of(MAC_A, 1u, (rcp_byte_bus_id_t)3u);
    rcp_avtp_addr_t           other   = addr_of(MAC_B, 2u, (rcp_byte_bus_id_t)4u);
    rcp_avtp_addr_t           eps[2];
    rcp_powerstate_manager_t *m;
    rcp_bytes_t               req;
    rcp_bytes_t               probe;
    rcp_stream_id_t           sleeper_resp = rcp_stream_id_make(MAC_A, 100u);
    rcp_stream_id_t           got_stream;

    /* TC18 §12.4.1 step (a)/(b): the server enables its interface, tests
     * whether the network is already available, and only spends a
     * WakeUp attempt once it is. REQ-PWRMODE-016: a false
     * network_available leaves the handshake retriable without
     * consuming wakeup_attempts. */
    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_iface_reenabled(&hs, false));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_NOT_STARTED, hs.step);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_EQUAL_UINT32(1u, hs.wakeup_attempts);

    /* TC18 §12.4.1 also sends the repetitive wake response on the
     * responder stream configured for the original sleep/standby
     * request. REQ-PWRMODE-017: rcp_powerstate_manager_handshake_begin()
     * now records that stream per-endpoint (sleeper's own resp stream,
     * distinct from sleeper's own request stream, `sleeper.stream_id`),
     * and rcp_powerstate_manager_wake_response_stream_id() returns it --
     * `other`, a different endpoint that never asked the server to
     * sleep, gets its own, independently-recorded stream. */
    eps[0] = sleeper;
    eps[1] = other;
    m      = rcp_powerstate_manager_new(eps, 2u);
    TEST_ASSERT_NOT_NULL(m);
    req = rcp_powerstate_manager_encode_entry_request(m, sleeper, RCP_PWRMODE_SLEEP, 7u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_TRUE(rcp_powerstate_manager_handshake_begin(m, sleeper, 3u, true, sleeper_resp));
    TEST_ASSERT_TRUE(rcp_powerstate_manager_wake_response_stream_id(m, sleeper, &got_stream));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(sleeper_resp, got_stream));
    TEST_ASSERT_FALSE(rcp_stream_id_equal(sleeper.stream_id, got_stream));

    /* `other` was never given a handshake -- no responder stream is
     * recorded for it, correctly distinguishing it from `sleeper`. */
    TEST_ASSERT_FALSE(rcp_powerstate_manager_wake_response_stream_id(m, other, &got_stream));

    probe = rcp_powerstate_manager_encode_wakeup_probe(m, sleeper, 7u);
    TEST_ASSERT_NOT_NULL(probe.data);

    rcp_bytes_free(&req);
    rcp_bytes_free(&probe);
    rcp_powerstate_manager_destroy(m);
}

/* ── §12.4.1: what actually terminates WakeUp repetition ───────────────────── */

static void test_wakeup_repetition_ignores_other_valid_avtpdus(void)
{
    const rcp_byte_bus_id_t bus = (rcp_byte_bus_id_t)3u;
    rcp_pwrmode_handshake_t hs;
    rcp_bytes_t             reply = rcp_ep_wakeup_encode_sleepcmd_response(bus,
                                                                          RCP_PWRMODE_ENTRY_OK,
                                                                          9u);

    TEST_ASSERT_NOT_NULL(reply.data);

    /* TC18 §12.4.1 stops the WakeUp repetition on receipt of ANY valid
     * AVTPDU from the sleep-request client, and defines no repeat limit.
     * c-RCP terminates only on an exact echo of the WakeUp message it sent:
     * the perfectly valid SleepCMD response below is not an echo... */
    TEST_ASSERT_FALSE(rcp_ep_wakeup_is_wakeup_echo(reply.data, reply.len, bus, 9u));

    /* ...so the handshake keeps repeating and then fails outright once the
     * implementation-added repeat limit is exhausted, degrading a hot start
     * that TC18 would have considered complete into a cold one. */
    rcp_pwrmode_handshake_init(&hs, 2u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_has_failed(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_has_failed(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_is_complete(&hs));

    rcp_bytes_free(&reply);
}

/* ── §12.4.1: completing the handshake, and the network-wake shortcut ──────── */

/* As of the REQ-PWRMODE-019 fix: completing the handshake now DOES
 * re-enable every registered endpoint -- but not via power.h's own
 * rcp_pwrmode_handshake_resume_queues() (which deliberately never
 * touches server.h, matching this codebase's established "pure
 * primitive, caller composes" layering -- lifecycle.h/discovery.h use
 * the identical pattern). The composition lives in mock.h's own
 * srv-aware rcp_mock_server_pwrmode_resume(), tested directly in
 * test_mock.c's test_pwrmode_resume_reenables_all_endpoints().
 *
 * As of the REQ-PWRMODE-020 fix: this test's own name is now the
 * opposite of what it asserts, kept (renamed) rather than deleted, since
 * it directly pins the correction. TC18 §12.4.1's "a TC14/TC10-network-
 * woken server... will directly check for the network availability and
 * proceed as before" means a network wake runs the SAME handshake a
 * pin/EP-signal wake does (skipping only step (a)'s literal interface
 * re-enable, a hardware-level nuance this module's state machine does
 * not represent specially) -- not skip the handshake outright. */
static void test_network_wake_now_requires_the_same_handshake_as_pin(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t  kind = RCP_PWRMODE_START_HOT;
    rcp_pwrmode_handshake_t   hs;

    TEST_ASSERT_TRUE(rcp_pwrmode_hotstart_required(RCP_PWRMODE_WAKE_VIA_NETWORK));

    /* No handshake driven yet -- falls back to the module's safe
     * default, COLD, exactly like an incomplete pin-wake handshake. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, NULL,
                                                  &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);

    /* A completed handshake yields HOT, same as a pin wake. */
    mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_resume_queues(&hs));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, &hs,
                                                  &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

/* ── §12.5: how StandBy and Sleep may be entered ───────────────────────────── */

static void test_sleep_entry_is_request_only_with_no_network_path(void)
{
    const rcp_byte_bus_id_t bus = (rcp_byte_bus_id_t)3u;
    rcp_bytes_t             req = rcp_ep_wakeup_encode_sleepcmd_request(bus, 4u);
    uint8_t                 tn  = 0u;

    /* TC18 §12.5: StandBy is entered only in response to an RCP request,
     * never from a network signal. Corrected 2026-08-10 (c-RCP-AUDIT-06,
     * issue #256 Group E): TC18 §13.7.2.3 Figure 22's SleepCMD wire
     * message carries no target-mode field at all -- it unconditionally
     * means Sleep, so there is no longer a wire-level "target mode
     * outside {StandBy, Sleep}" for this layer to refuse. The
     * StandBy-vs-Sleep selection (and StandBy's outright rejection, since
     * it has no wire encoding at all) now lives one layer up, in
     * rcp_powerstate_manager_encode_entry_request() -- see
     * test_encode_entry_request_rejects_standby() in test_powerstate.c.
     * What remains true and testable at *this* layer is the other half of
     * the deviation pin: SleepCMD is a fixed request/response exchange
     * with no alternate network-triggered wire encoding at all. */
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_sleepcmd_request(req.data, req.len, bus, &tn));
    TEST_ASSERT_EQUAL_UINT8(4u, tn);

    rcp_bytes_free(&req);
}

static void test_network_sleep_cannot_be_wired_to_standby(void)
{
    rcp_pwrmode_entry_gate_t gate = {true, true, true};
    rcp_pwrmode_t            mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t kind;

    /* REQ-PWRMODE-021 (TC18 §12.5): "StandBy can only be initiated via
     * request to the RC Server" -- power.h's rcp_pwrmode_commit_network_sleep()
     * is the ONLY entry point a caller integrating a real TC14/TC10 signal
     * uses, and it has no target parameter at all: the only power mode it
     * can ever produce is RCP_PWRMODE_SLEEP. This makes the exclusivity
     * impossible to violate by construction, not merely true by omission
     * (there being no network path modelled at all, as the pre-fix reading
     * of this deviation pin observed). */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_commit_network_sleep(&mode, &gate, true, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
    TEST_ASSERT_NOT_EQUAL(RCP_PWRMODE_STANDBY, mode);
}

static void test_network_sleep_applies_the_same_conditions_as_a_normal_request(void)
{
    rcp_pwrmode_entry_gate_t satisfied = {true, true, true};
    rcp_pwrmode_entry_gate_t refusing  = {false, true, true};
    rcp_pwrmode_t            mode      = RCP_PWRMODE_STANDBY;

    /* REQ-PWRMODE-022 (TC18 §12.5): "Sleep can be initiated either via the
     * network with a valid TC14/TC10 sleep request or via request to the
     * RC Server." rcp_pwrmode_commit_network_sleep() delegates to the exact
     * same rcp_pwrmode_commit_entry() a normal (RCP-request) sleep entry
     * uses -- "the same conditions apply as for a normal sleep request"
     * (TC18's own words): the lost-wakeup race is closed and the
     * response-sent ordering is enforced identically, and a network
     * trigger is refused under the identical gate a normal request would
     * be refused under. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_ENTRY_REFUSED,
                      rcp_pwrmode_commit_network_sleep(&mode, &refusing, true, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_STANDBY, mode);

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_commit_network_sleep(&mode, &satisfied, true, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
}

/* ── §12.5: a sleep request tracked per remote peer, and now authorized ────── */

static void test_sleep_request_moves_one_endpoint_only(void)
{
    rcp_avtp_addr_t           sleeper = addr_of(MAC_A, 1u, (rcp_byte_bus_id_t)3u);
    rcp_avtp_addr_t           other   = addr_of(MAC_B, 2u, (rcp_byte_bus_id_t)4u);
    rcp_avtp_addr_t           eps[2];
    rcp_powerstate_manager_t *m;
    rcp_bytes_t               req;
    rcp_bytes_t               resp;

    eps[0] = sleeper;
    eps[1] = other;
    m      = rcp_powerstate_manager_new(eps, 2u);
    TEST_ASSERT_NOT_NULL(m);

    req = rcp_powerstate_manager_encode_entry_request(m, sleeper, RCP_PWRMODE_SLEEP, 7u);
    TEST_ASSERT_NOT_NULL(req.data);
    resp = rcp_ep_wakeup_encode_sleepcmd_response(sleeper.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 7u);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK,
                      rcp_powerstate_manager_apply_entry_response(m, sleeper, resp.data,
                                                                  resp.len));

    /* Not a TC18 deviation: rcp_powerstate_manager_t is this library's own
     * CLIENT-side bookkeeping of multiple different remote peers, one
     * endpoint_entry_t per registered addr -- "the entire RC Server
     * implementation" TC18 §12.5 describes is each of THOSE remote
     * servers' own, single, internal mode, which a real server-side
     * composition (power.h's rcp_pwrmode_commit_entry(), operating on one
     * rcp_pwrmode_t) models correctly -- see that function's own doc
     * comment. sleeper/other below are two DIFFERENT remote servers this
     * client happens to track, not two endpoints of the same server, so
     * each keeping its own mode is the intended design, not a gap. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, rcp_powerstate_manager_mode(m, sleeper));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, other));

    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

static void test_sleepcmd_requires_root_client_authorization(void)
{
    rcp_lifecycle_writer_ctx_t authorized;
    rcp_lifecycle_writer_ctx_t unauthorized;

    memset(&authorized, 0, sizeof(authorized));
    memset(&unauthorized, 0, sizeof(unauthorized));
    authorized.via_root_client_ep0  = true;
    unauthorized.via_discovery_stream = true;

    /* REQ-PWRMODE-023 (TC18 §12.5): "The RC Client that is allowed to
     * access the RC Server endpoint can request the entire RC Server
     * implementation to enter standby or sleep mode." Only
     * via_root_client_ep0 authorizes a SleepCMD; an unqualified
     * discovery-stream sender -- sufficient for configuration during
     * bring-up, per lifecycle.h -- is not the same authorized client. */
    TEST_ASSERT_TRUE(rcp_ep_wakeup_sleepcmd_writable(authorized));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_sleepcmd_writable(unauthorized));
}

/* ── §12.5: commit_entry re-checks the gate, closing the lost-wakeup race ──── */

static void test_commit_entry_re_checks_the_gate_and_aborts_the_race(void)
{
    rcp_pwrmode_entry_gate_t gate = {true, true, true};
    rcp_pwrmode_t            mode = RCP_PWRMODE_NORMAL;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_OK, rcp_pwrmode_check_entry(&gate));

    /* A wake source asserts while the sleep request is still being
     * processed, so a caller re-samples the gate before calling
     * rcp_pwrmode_commit_entry() -- and gets a fresh REFUSED reading. */
    gate.wup_status_clear = false;

    /* REQ-PWRMODE-024 (TC18 §12.5): "If an event defined as wake-up source
     * in the wake-up endpoint occurs during the processing of the
     * sleep-request, the request is not executed." commit_entry()
     * re-validates the (freshly re-sampled) gate itself immediately before
     * the mode would change, closing the race a single up-front
     * rcp_pwrmode_check_entry() call would leave open: the entry is
     * refused and the mode is left unchanged, unlike plain
     * rcp_pwrmode_transition(), which has no gate of its own at all. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_ENTRY_REFUSED,
                      rcp_pwrmode_commit_entry(&mode, RCP_PWRMODE_SLEEP, &gate, true, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
}

/* ── §12.5: the mode change happens only once the response has been sent ──── */

static void test_commit_entry_requires_the_response_to_have_been_sent(void)
{
    rcp_pwrmode_entry_gate_t gate = {true, true, true};
    rcp_pwrmode_t            mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t kind;

    /* REQ-PWRMODE-026 (TC18 §12.5): "The RC Server enters standby or
     * sleep mode, as soon as the go to sleep/standby response to the
     * standby/sleep request has been sent." response_sent is this
     * library's own caller-supplied proof of that precondition -- the
     * gate alone is satisfied here, but the entry is still refused because
     * the response has not (yet) been transmitted. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_ENTRY_REFUSED,
                      rcp_pwrmode_commit_entry(&mode, RCP_PWRMODE_SLEEP, &gate, false, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);

    /* Once the caller has actually sent the response, the same gate now
     * commits the transition. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_commit_entry(&mode, RCP_PWRMODE_SLEEP, &gate, true, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

/* ── §12.5: the network-sleep return value IS the LPS-suppression signal ──── */

static void test_network_sleep_refusal_return_value_gates_the_lps_confirmation(void)
{
    rcp_pwrmode_entry_gate_t refusing  = {false, true, true};
    rcp_pwrmode_entry_gate_t satisfied = {true, true, true};
    rcp_pwrmode_t            refused_mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_t            ok_mode      = RCP_PWRMODE_NORMAL;

    /* REQ-PWRMODE-027 (TC18 §12.5): "If the go to sleep conditions are not
     * fulfilled the network PHY shall not signal a TC14/TC10 LPS as
     * confirmation." This library has no PHY-signalling surface of its
     * own (see rcp_pwrmode_commit_network_sleep()'s own doc comment for
     * why, and the same scoping precedent as network_available) -- its
     * RCP_PWRMODE_OK vs. RCP_PWRMODE_ERR_ENTRY_REFUSED return value IS the
     * signal an integrator's own PHY driver gates LPS assertion on: only
     * assert LPS when this function returns RCP_PWRMODE_OK, never on
     * RCP_PWRMODE_ERR_ENTRY_REFUSED. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_ENTRY_REFUSED,
                      rcp_pwrmode_commit_network_sleep(&refused_mode, &refusing, true, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, refused_mode); /* PHY: do NOT signal LPS */

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_commit_network_sleep(&ok_mode, &satisfied, true, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, ok_mode); /* PHY: safe to signal LPS */
}

/* ── §12.5: the gate's fields are documented server-wide aggregates ───────── */

static void test_entry_gate_is_scoped_to_one_endpoint_and_one_queue(void)
{
    rcp_pwrmode_entry_gate_t gate;
    rcp_server_endpoint_t    wakeup_ep;
    rcp_server_endpoint_t    busy_ep;
    rcp_bytes_t              frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&wakeup_ep, true);
    rcp_server_endpoint_init(&busy_ep, false);

    /* A second endpoint is plainly not idle: it holds an undrained request. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&busy_ep, frame.data, frame.len, NULL));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&busy_ep));

    /* REQ-PWRMODE-025 (TC18 §12.5): entry is refused when AT LEAST ONE
     * endpoint of the whole server is not idle, or AT LEAST ONE responder
     * queue still holds an untransmitted message -- power.h's
     * endpoint_idle/response_queue_empty fields are documented as those
     * server-wide aggregates, not one endpoint's/one queue's own state. A
     * caller that correctly ANDs every endpoint's idleness into
     * endpoint_idle (rather than reporting only the wake-up endpoint's
     * own, as a narrower reading of this struct's pre-fix docs invited)
     * sees the busy second endpoint here and refuses entry. */
    gate.wup_status_clear     = true;
    gate.endpoint_idle        = (rcp_server_endpoint_queue_len(&wakeup_ep) == 0u) &&
                                 (rcp_server_endpoint_queue_len(&busy_ep) == 0u);
    gate.response_queue_empty = true;
    TEST_ASSERT_FALSE(gate.endpoint_idle);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&gate));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&wakeup_ep);
    rcp_server_endpoint_destroy(&busy_ep);
}

/* ── §13.7.2.3 step 1: admission suspends during the sleep drain ──────────── */

static void test_admission_is_suspended_during_the_sleep_drain(void)
{
    rcp_server_endpoint_t wakeup_ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    uint8_t               request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&wakeup_ep, true);

    /* Before any sleep request has arrived, admission behaves exactly as
     * ever -- an enabled endpoint executes a standard request immediately. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&wakeup_ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));

    /* REQ-PWRMODE-028 (TC18 §13.7.2.3 step 1): "on receipt of a sleep
     * request the server shall stop entering newly arriving requests into
     * endpoint queues while the drain proceeds." A caller processing a
     * SleepCMD sets admission_suspended before beginning its drain (steps
     * 2-3, which power.h's rcp_pwrmode_check_entry()/
     * rcp_pwrmode_commit_entry() already implement) -- a request arriving
     * mid-drain is refused outright, not queued and not executed. */
    rcp_server_endpoint_set_admission_suspended(&wakeup_ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_SUSPENDED,
                      rcp_server_endpoint_admit(&wakeup_ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_server_endpoint_queue_len(&wakeup_ep));

    /* If the entry is ultimately refused (rcp_pwrmode_check_entry() said
     * REFUSED, or response_sent never became true), a caller resumes
     * normal admission by clearing the flag again. */
    rcp_server_endpoint_set_admission_suspended(&wakeup_ep, false);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&wakeup_ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&wakeup_ep);
}

/* ── §12.3.1.3: requests arriving at a disabled endpoint ───────────────────── */

/* REQ-SRV-015 IMPLEMENTED (issue #336, GBB half closed using REQ-ACF-032's
 * new rcp_acf_peek_gbb_request_type()): TC18 §12.3.1.3: a disabled
 * endpoint still executes CONFIGURATION requests immediately and queues
 * only operational ones. rcp_server_endpoint_submit() now inspects a
 * request's own evt[2:0]: 111b (TC18 Table 33's own universal "EP_func
 * configuration write" meaning, §12.7.1) is executed immediately even
 * while disabled -- including, per this rule, the very write that would
 * set ep_enable itself -- for an ABB request AND for a GBB request of any
 * kind except Compound Wait (whose own evt[2:0]=111b means something
 * else entirely, §13.5.1 -- see the deviation-pin test below); any other
 * evt[2:0] value (an operational request) is still queued, as before. */
static void test_disabled_endpoint_executes_config_requests_immediately(void)
{
    rcp_server_endpoint_t       ep;
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 abb_config_frame;
    rcp_compound_step_t         step;
    rcp_bytes_t                 gbb_config_frame;

    hdr.byte_bus_id     = (rcp_byte_bus_id_t)5u;
    hdr.transaction_num = 0x42u;
    hdr.evt             = 0x07u; /* evt[2:0] = 111b: configuration write */
    abb_config_frame    = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(abb_config_frame.data);

    rcp_server_endpoint_init(&ep, false);
    TEST_ASSERT_TRUE(rcp_server_endpoint_submit(&ep, abb_config_frame.data,
                                                abb_config_frame.len, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_server_endpoint_queue_len(&ep));
    rcp_bytes_free(&abb_config_frame);
    rcp_server_endpoint_destroy(&ep);

    /* A GBB Compound request (not Compound Wait) with evt[2:0] = 111b is
     * also executed immediately -- Table 33's own config-write meaning
     * applies to it exactly as it does to an ABB request. */
    memset(&step, 0, sizeof(step));
    gbb_config_frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND,
                                                    (rcp_byte_bus_id_t)5u, &step, 0x07u, 0x43u,
                                                    NULL, 0u);
    TEST_ASSERT_NOT_NULL(gbb_config_frame.data);

    rcp_server_endpoint_init(&ep, false);
    TEST_ASSERT_TRUE(rcp_server_endpoint_submit(&ep, gbb_config_frame.data,
                                                gbb_config_frame.len, NULL));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_server_endpoint_queue_len(&ep));

    rcp_bytes_free(&gbb_config_frame);
    rcp_server_endpoint_destroy(&ep);
}

/* REQ-SRV-015 DEVIATION PIN (Compound Wait, genuinely unaffected by this
 * fix -- not an oversight): a Compound Wait request's own evt[2:0] means
 * an 8-way comparison-operator selector under §13.5.1, never a
 * configuration-write signal, even when its bit pattern happens to equal
 * 111b -- rcp_server_endpoint_submit() checks rcp_request_type_is_compound_
 * wait() specifically to exclude it from the config-write fast path. An
 * ordinary ABB operational request (evt[2:0] != 111b) is also still
 * queued, unaffected by this fix. */
static void test_disabled_endpoint_still_queues_operational_and_compound_wait_requests(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 0x42u);
    uint8_t               request_type = 0xFFu;
    rcp_compound_step_t   step;
    rcp_bytes_t           wait_frame;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, false);

    /* Operational ABB request (evt[2:0] == 0, not 111b): still queued. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, frame.data, frame.len, NULL));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&ep));

    /* admit() takes the same path for a standard request -- it too queues
     * rather than executing, confirming the deviation isn't submit()-only. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_QUEUED,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);
    TEST_ASSERT_EQUAL_UINT(2u, rcp_server_endpoint_queue_len(&ep));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);

    /* A Compound Wait request with evt[2:0] = 111b (a real comparison
     * mode under §13.5.1, not a config-write signal) is still queued, not
     * mistaken for a configuration request. */
    memset(&step, 0, sizeof(step));
    wait_frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT,
                                              (rcp_byte_bus_id_t)5u, &step, 0x07u, 0x44u,
                                              NULL, 0u);
    TEST_ASSERT_NOT_NULL(wait_frame.data);

    rcp_server_endpoint_init(&ep, false);
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, wait_frame.data, wait_frame.len, NULL));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&ep));

    rcp_bytes_free(&wait_frame);
    rcp_server_endpoint_destroy(&ep);
}

/* FIXED 2026-08-12 (issue #201, REQ-SRV-016): TC18 §12.3.1.3 -- "if
 * requested an acknowledge is sent after storing the request." evt[3] is
 * TC18 §13.5's own universal (endpoint-type-independent) acknowledge-
 * request bit, so rcp_server_endpoint_submit()'s new out_ack can check it
 * without needing REQ-SRV-015's own still-open endpoint-type classifier. */
static void test_disabled_endpoint_queuing_emits_requested_acknowledge(void)
{
    rcp_server_endpoint_t       ep;
    rcp_acf_byte_message_info_t req_hdr = {0};
    rcp_bytes_t                 frame_wants_ack, frame_no_ack, ack;
    rcp_acf_byte_message_info_t ack_hdr;
    const uint8_t                *ack_payload;
    size_t                        ack_payload_len;

    rcp_server_endpoint_init(&ep, false);

    /* evt[3] = 1 (0x08) requests an acknowledge; evt[2:0] is left 0 --
     * this bit is independent of whatever per-endpoint meaning evt[2:0]
     * carries (TC18 §13.5's own opening statement, before its per-
     * endpoint-type table). */
    req_hdr.byte_bus_id     = (rcp_byte_bus_id_t)5u;
    req_hdr.transaction_num = 0x42u;
    req_hdr.evt             = 0x08u;
    frame_wants_ack = rcp_acf_encode_abb(&req_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame_wants_ack.data);

    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, frame_wants_ack.data, frame_wants_ack.len,
                                                 &ack));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&ep));
    TEST_ASSERT_NOT_NULL(ack.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(ack.data, ack.len, &ack_hdr, &ack_payload,
                                        &ack_payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&ack_hdr));
    TEST_ASSERT_EQUAL_UINT8(5u, ack_hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0x42u, ack_hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT(0u, ack_payload_len);
    rcp_bytes_free(&ack);
    rcp_bytes_free(&frame_wants_ack);

    /* A request that did NOT set evt[3] gets no ack -- storing it is
     * silent, exactly as before this fix. */
    req_hdr.evt = 0x00u;
    frame_no_ack = rcp_acf_encode_abb(&req_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame_no_ack.data);

    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, frame_no_ack.data, frame_no_ack.len, &ack));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_server_endpoint_queue_len(&ep));
    TEST_ASSERT_NULL(ack.data);

    rcp_bytes_free(&frame_no_ack);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §13.7.1.1: the cyclic heartbeat register without an emitter ───────────── */

static void test_response_queue_flush_period_is_carried_but_inert(void)
{
    rcp_regmap_response_queue_cfg_t cfg;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.flush_time_us);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.flush_on_count);

    /* TC18 §13.7.1.1 requires the server to emit a cyclic heartbeat AVTPDU
     * every flush period, and specifically an EMPTY NTSCF-only PDU -- an
     * NTSCF header carrying no ACF messages at all -- when the period
     * elapses with an empty response queue. c-RCP carries the periodicity
     * register faithfully (it round-trips as plain R/W data below), and
     * respqueue.h's rcp_respqueue_should_flush_by_time() now reads
     * flush_time_us to recognize the trigger (REQ-RMAP-064) and composes
     * with rcp_avtp_encode_ntscf(hdr, NULL, 0) to construct the empty PDU
     * itself (REQ-RMAP-065's primitive half; see test_tc18_gaps_regmap.c's
     * test_flush_time_trigger_and_empty_heartbeat_are_composable()) -- but
     * this library still ships no emitter that SCHEDULES that composition
     * against a real clock: a client watching for liveness still sees
     * nothing unless an integrator drives it. */
    cfg.flush_time_us  = 20000u;
    cfg.flush_on_count = 8u;
    TEST_ASSERT_EQUAL_UINT32(20000u, cfg.flush_time_us);
    TEST_ASSERT_EQUAL_UINT16(8u, cfg.flush_on_count);
}

/* ── §13.7.1.3 Table 37: RC-Server-issued PTP trigger signals ──────────────── */

/* REQ-SRV-018 PARTIAL (issue #201): rcp_server_gptp_trigger_evaluate()
 * (server.h/server.c) now derives Table 37's own trigger signal 0/1 from a
 * genuine gPTP lock transition -- the piece this test used to pin as
 * entirely missing. Composed by hand with the pre-existing
 * rcp_server_endpoint_notify_trigger(), the derived signal correctly arms
 * a stored triggered request. Deliberately still PARTIAL, not
 * IMPLEMENTED: nothing in this library's own dispatch loop calls
 * evaluate()+notify_trigger() together automatically on every tick yet --
 * the same "primitive complete, dispatch wiring deferred" disposition
 * already established for REQ-GPIO-033/REQ-ADC-031/REQ-SRV-016 -- so a
 * caller still has to drive both calls itself each time it observes a new
 * gptp_locked value. */
static void test_gptp_trigger_evaluate_derives_signal_and_composes_with_notify(void)
{
    rcp_server_endpoint_t           ep;
    rcp_sequencer_table_t           seqs = {NULL, 0u};
    rcp_server_tick_ctx_t           ctx;
    rcp_triggered_step_t            step;
    rcp_bytes_t                     frame;
    uint8_t                         request_type = 0xFFu;
    size_t                          idx          = 0u;
    rcp_server_gptp_trigger_state_t trig;
    uint8_t                         signal_nr = 0xFFu;

    memset(&step, 0, sizeof(step));
    step.trigger_source_ep = 0u; /* EP0 -- where Table 37's server signals originate */
    step.trigger_signal_nr = RCP_SERVER_GPTP_TRIGGER_ESTABLISHED; /* signal 0 */
    rcp_server_endpoint_init(&ep, true);
    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, (rcp_byte_bus_id_t)5u,
                                          &step, 1u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, &idx, NULL));

    memset(&ctx, 0, sizeof(ctx));
    ctx.sequencers    = &seqs;
    ctx.endpoint_idle = true;

    rcp_server_gptp_trigger_state_init(&trig);

    /* The very first observation is never itself a transition -- no
     * previous state exists yet to detect an edge against. */
    TEST_ASSERT_FALSE(rcp_server_gptp_trigger_evaluate(&trig, false, &signal_nr));
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* unlocked -> locked is a genuine edge: signal 0 (ESTABLISHED). Merely
     * observing it changes nothing on its own -- select_due() only reads
     * ctx.gptp_locked as a gate for timed requests, not as a trigger
     * source -- until the derived signal is actually delivered via
     * notify_trigger(), matching this endpoint's own trigger_source_ep/
     * trigger_signal_nr selection. */
    ctx.gptp_locked = true;
    TEST_ASSERT_TRUE(rcp_server_gptp_trigger_evaluate(&trig, true, &signal_nr));
    TEST_ASSERT_EQUAL_UINT8(RCP_SERVER_GPTP_TRIGGER_ESTABLISHED, signal_nr);
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_notify_trigger(&ep, 0u, signal_nr));
    TEST_ASSERT_TRUE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* A repeated observation at the SAME lock state is not a transition:
     * no signal fires. */
    signal_nr = 0xFFu;
    TEST_ASSERT_FALSE(rcp_server_gptp_trigger_evaluate(&trig, true, &signal_nr));
    TEST_ASSERT_EQUAL_UINT8(0xFFu, signal_nr); /* left unchanged */

    /* locked -> unlocked is the symmetric edge: signal 1 (LOST). */
    TEST_ASSERT_TRUE(rcp_server_gptp_trigger_evaluate(&trig, false, &signal_nr));
    TEST_ASSERT_EQUAL_UINT8(RCP_SERVER_GPTP_TRIGGER_LOST, signal_nr);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* REQ-SRV-018: a first-ever observation is never itself a transition, even
 * when it disagrees with rcp_server_gptp_trigger_state_init()'s own default
 * previous_locked=false -- there is no genuine previous state to compare
 * against yet, regardless of which value locked happens to be on that
 * first call. Distinct from the previous test's own first call (which
 * happens to start at locked=false, agreeing with the default, and so
 * cannot by itself distinguish "correctly guarded by has_previous" from
 * "accidentally correct because the two values already matched"). */
static void test_gptp_trigger_evaluate_first_observation_never_an_edge(void)
{
    rcp_server_gptp_trigger_state_t trig;
    uint8_t                         signal_nr = 0xFFu;

    rcp_server_gptp_trigger_state_init(&trig);
    TEST_ASSERT_FALSE(rcp_server_gptp_trigger_evaluate(&trig, true, &signal_nr));
    TEST_ASSERT_EQUAL_UINT8(0xFFu, signal_nr); /* left unchanged */

    /* The second call now has a genuine previous state (true) to compare
     * against: staying at true is still not a transition. */
    TEST_ASSERT_FALSE(rcp_server_gptp_trigger_evaluate(&trig, true, &signal_nr));
    TEST_ASSERT_EQUAL_UINT8(0xFFu, signal_nr);
}

/* ── §12.7.10 Table 28: sequencer disable, ownership, and register wiring ──── */

/* REQ-SEQ-012 IMPLEMENTED (issue #201): TC18 §12.7.10 Table 28 -- a
 * sequencer whose Seq_state register has been manually written to 0 is
 * DISABLED: no compound or compound-wait step bound to it may become
 * executable, and no advance may move it out of 0. rcp_compound_start_
 * condition_met() and rcp_compound_advance_guard() (request_compound.h/
 * .c) both now check current==0 explicitly, before either function's own
 * ordinary start_state comparison -- including the "start in any state"
 * wildcard (start_state==0), which would otherwise treat a disabled
 * sequencer as satisfying every step unconditionally. */
static void test_sequencer_zero_state_disables_start_condition_and_advance(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4u);
    rcp_compound_step_t   step;
    uint8_t               state = 0xFFu;

    TEST_ASSERT_EQUAL_UINT16(4u, table.count);
    memset(&step, 0, sizeof(step));

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0u, 0u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0u, &state));
    TEST_ASSERT_EQUAL_UINT8(0u, state);

    /* A step whose own start_state is the "any state" wildcard (0) no
     * longer starts against a disabled sequencer -- disabled is not
     * itself a state any step may start from. */
    step.start_state     = 0u;
    step.sequencer_index = 0u;
    TEST_ASSERT_FALSE(rcp_compound_start_condition_met(&table, &step));

    /* A step whose start_state happens to also be a specific nonzero
     * value never matches a disabled (0) sequencer either -- unaffected
     * by the wildcard case above, but confirmed directly rather than
     * assumed. */
    step.start_state = 5u;
    TEST_ASSERT_FALSE(rcp_compound_start_condition_met(&table, &step));

    /* rcp_compound_advance_guard() -- the gate rcp_compound_tick()/
     * rcp_compound_wait_tick() both check before applying next_state --
     * also refuses to advance a disabled sequencer, even for a step
     * whose own start_state happens to equal 0 too (the one case where a
     * naive current==step->start_state comparison would otherwise have
     * matched). */
    step.start_state = 0u;
    TEST_ASSERT_FALSE(rcp_compound_advance_guard(&table, &step));

    /* Explicitly rewriting the sequencer to a nonzero state re-enables
     * it: the very next call correctly starts/advances again. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0u, 7u));
    step.start_state = 7u;
    TEST_ASSERT_TRUE(rcp_compound_start_condition_met(&table, &step));
    TEST_ASSERT_TRUE(rcp_compound_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_sequencer_zero_state_ownership_and_regmap_wiring(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4u);
    rcp_regmap_general_t  map;
    uint8_t               state = 0xFFu;

    TEST_ASSERT_EQUAL_UINT16(4u, table.count);

    /* Table 28 also gives each sequencer a Request_stream_index naming the
     * one RC Client permitted to access it. rcp_sequencer_table_t is a bare
     * state array with no owner, and set_state() takes no requester
     * identity, so any client can overwrite another's sequencer -- including
     * one configured as its rx_safestate_sequencer. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0u, 9u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0u, &state));
    TEST_ASSERT_EQUAL_UINT8(9u, state);

    /* This bare pair -- a freshly-init'd regmap struct and a separately-
     * created table, with no mock server tying them together -- is
     * exactly this test's own point: neither register that would expose
     * this table over EP0 is bound to it by the primitives alone.
     * svr_sequencers_max (REQ-RMAP-028, kept synced only by mock.c's
     * rcp_mock_server_set_sequencer_count(), not consulted here) and the
     * svr_sequencer_state_ptr register (REQ-RMAP-038) stay at their
     * initialized zeros while a 4-sequencer table exists. */
    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT8(0u, map.svr_sequencers_max);
    TEST_ASSERT_EQUAL_UINT16(0u, map.svr_sequencer_state_ptr);

    rcp_sequencer_table_free(&table);
}

/* ── §11.2 / §11.2.1: TSCF-carried timed requests ──────────────────────────── */

/* FIXED 2026-08-13 (issue #338, tc18-gap backlog PR C, REQ-TIMED-012):
 * rcp_server_endpoint_admit() now takes tv/avtp_timestamp/
 * gptp_reference_now. tv=false is byte-for-byte the old behavior
 * (postponement never applies -- every existing caller of this module
 * is unaffected); this test used to pin exactly that as a gap. It now
 * asserts the conforming positive case instead, per this file's own
 * established "a gap-pinning test failing after a fix means rewrite it
 * to the conforming expectation" convention. */
static void test_ntscf_standard_request_still_executes_immediately(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    uint8_t               request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u,
                                                &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* The actual TC18 §11.2/§11.2.1 rule closed: tv=true postpones a
 * standard request via the request store instead of executing it
 * immediately -- it becomes due only once rcp_server_tick_ctx_t's own
 * gptp_now reaches the reconstructed presentation time, and never while
 * gptp_locked is false (fail-closed, the same rule TIMED's own
 * presentation_time already follows). reference_now is chosen as 0 so
 * the reconstructed instant equals avtp_timestamp verbatim (no
 * wraparound arithmetic to reason about -- rcp_avtp_extend_timestamp()'s
 * own reconstruction math is already directly unit-tested in
 * tests/test_avtp.c). */
static void test_tscf_standard_request_postponed_until_presentation_time(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    uint8_t               request_type = 0xFFu;
    size_t                idx = 0u;
    rcp_server_tick_ctx_t ctx;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, true, 5000000u,
                                                0u, &request_type, &idx, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type); /* standard, not conditional -- no repurposed opcode */

    memset(&ctx, 0, sizeof(ctx));
    ctx.endpoint_idle = true;

    /* Locked but one tick early: not yet due. */
    ctx.gptp_locked = true;
    ctx.gptp_now     = 4999999u;
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* Reached the reconstructed instant, but time base not locked: still
     * fail-closed, the same rule TIMED's own auxiliary_condition_met()
     * already applies. */
    ctx.gptp_locked = false;
    ctx.gptp_now     = 5000000u;
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* Both conditions satisfied: due. */
    ctx.gptp_locked = true;
    TEST_ASSERT_TRUE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* A conditional request under a TSCF header is gated by BOTH its own
 * kind-specific condition AND the new envelope-level presentation gate,
 * independently -- neither alone is sufficient. Uses TRIGGERED (armed
 * immediately, no sequencer table needed) rather than COMPOUND to keep
 * the fixture minimal. */
static void test_tscf_conditional_request_needs_both_its_own_condition_and_the_gate(void)
{
    rcp_server_endpoint_t   ep;
    rcp_triggered_step_t    step;
    rcp_bytes_t             frame;
    uint8_t                 request_type = 0xFFu;
    size_t                  idx          = 0u;
    rcp_server_tick_ctx_t   ctx;

    memset(&step, 0, sizeof(step)); /* trigger_threshold 0: fires on the first occurrence */
    step.trigger_source_ep    = 0u;
    step.trigger_signal_nr    = 0u;
    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, (rcp_byte_bus_id_t)5u,
                                          &step, 1u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_server_endpoint_init(&ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, true, 5000000u,
                                                0u, &request_type, &idx, NULL));

    memset(&ctx, 0, sizeof(ctx));
    ctx.endpoint_idle = true;
    ctx.gptp_locked   = true;
    ctx.gptp_now      = 5000000u; /* presentation gate open */

    /* Presentation gate open, but the trigger's own threshold never
     * reached: still not due -- the envelope gate does not bypass the
     * kind's own condition. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* Trigger threshold reached, but presentation gate not yet open:
     * still not due -- the kind's own condition does not bypass the
     * envelope gate either. */
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_notify_trigger(&ep, 0u, 0u));
    ctx.gptp_now = 0u;
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* Both satisfied: due. */
    ctx.gptp_now = 5000000u;
    TEST_ASSERT_TRUE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* ── issue #463 (REQ-SRV-016): rcp_server_endpoint_admit_with_ack() ────────── */

/* TC18 §12.9.5's own generic wording -- "an acknowledge is given if
 * requested as soon as the new request has been successfully queued for
 * execution in the addressed endpoint's request storage" -- is worded
 * over the whole endpoint request storage, not scoped to a Standard
 * request the way rcp_server_endpoint_submit()'s own REQ-SRV-016 fix
 * (issue #201) reads. This test exercises admit_under_tscf_gate()'s own
 * new out_ack build (server.c) directly: a Standard request postponed
 * purely by REQ-TIMED-012's TSCF presentation-time gate (the exact
 * scenario test_tscf_standard_request_postponed_until_presentation_time()
 * above already covers for admission itself) whose own evt[3] asks for an
 * acknowledge gets one built immediately at admission time -- TC18's own
 * "as soon as... successfully queued" wording, not deferred until the
 * request later becomes due. */
static void test_tscf_pending_standard_request_emits_requested_acknowledge(void)
{
    rcp_server_endpoint_t       ep;
    rcp_bytes_t                 frame = standard_abb_with_evt((rcp_byte_bus_id_t)5u, 0x42u, 0x08u);
    uint8_t                     request_type = 0xFFu;
    size_t                      idx          = 0u;
    rcp_bytes_t                 ack          = {0};
    rcp_acf_byte_message_info_t ack_hdr;
    const uint8_t                *ack_payload;
    size_t                        ack_payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit_with_ack(&ep, frame.data, frame.len, 0u, true,
                                                          5000000u, 0u, &request_type, &idx, NULL,
                                                          &ack));
    TEST_ASSERT_NOT_NULL(ack.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(ack.data, ack.len, &ack_hdr, &ack_payload,
                                        &ack_payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&ack_hdr));
    TEST_ASSERT_EQUAL_UINT8(0u, ack_hdr.err); /* success, not the #454 rejection shape */
    TEST_ASSERT_EQUAL_UINT8(5u, ack_hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0x42u, ack_hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT(0u, ack_payload_len);

    rcp_bytes_free(&ack);
    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* evt[3] clear (no acknowledge requested): admission still succeeds
 * (RCP_SERVER_ADMIT_PENDING, unaffected -- this fix only ever ADDS a
 * response, never changes admission itself), but out_ack stays zeroed,
 * exactly as rcp_server_endpoint_submit()'s own REQ-SRV-016 gate already
 * behaves for the plain Standard-queuing case. */
static void test_tscf_pending_standard_request_no_acknowledge_when_evt3_clear(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb_with_evt((rcp_byte_bus_id_t)5u, 0x43u, 0x00u);
    uint8_t               request_type = 0xFFu;
    size_t                idx          = 0u;
    rcp_bytes_t           ack          = {0};

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit_with_ack(&ep, frame.data, frame.len, 0u, true,
                                                          5000000u, 0u, &request_type, &idx, NULL,
                                                          &ack));
    TEST_ASSERT_NULL(ack.data);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* FIXED (REQ-TIMED-013, already closed in an earlier batch -- reconfirmed
 * here rather than left as a stale deviation pin): TC18 §11.2/§11.2.1
 * also names an ACF_ABB encoding for a timed request whose presentation
 * time rides in the enclosing TSCF header, distinct from the ACF_GBB
 * repurposed-timestamp form rcp_timed_encode_request() emits.
 * rcp_timed_encode_request_tscf() (request_timed.h) is that encoder. */
static void test_timed_encode_request_tscf_produces_a_real_abb_message(void)
{
    static const uint8_t        mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 timed;
    rcp_avtp_tscf_header_t      out_hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      msg_type = 0u;

    hdr.byte_bus_id     = (rcp_byte_bus_id_t)5u;
    hdr.transaction_num = 1u;
    hdr.op              = (uint8_t)RCP_ACF_OP_WRITE;

    timed = rcp_timed_encode_request_tscf(&hdr, NULL, 0u, rcp_stream_id_make(mac, 1u),
                                           5000000u, 1u);
    TEST_ASSERT_NOT_NULL(timed.data);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                      rcp_avtp_decode_tscf(timed.data, timed.len, &out_hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(1u, out_hdr.tv);
    TEST_ASSERT_EQUAL_UINT32(5000000u, out_hdr.avtp_timestamp);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(payload, payload_len, &msg_type));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&timed);
}

/* ── §12.7.7: the per-stream request watchdog is never kicked ──────────────── */

static void busy_wait_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();

    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait: no sleep primitive is exported by rcp/clock.h */
    }
}

static void test_watchdog_overflows_despite_continuous_requests(void)
{
    rcp_watchdog_stream_cfg_t stream = {7u, true, 40u, true, true};
    rcp_watchdog_config_t     cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t    *k;
    rcp_server_endpoint_t     ep;
    rcp_bytes_t               frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    int                       elapsed_ms = 0;
    bool                      overflowed = false;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);

    /* TC18 §12.7.7: the per-stream watchdog is reset with EVERY request
     * received from that RC Client, so it measures the gap between
     * consecutive requests. rcp_watchdog_keeper_kick() implements the reset
     * correctly but has no production call site -- no receive path in the
     * library kicks it. Delivering a request every 10 ms on stream 7 for far
     * longer than its 40 ms timeout therefore still overflows the watchdog,
     * driving a live, perfectly responsive client into its safe state. */
    while (elapsed_ms < 1000 && !overflowed) {
        TEST_ASSERT_TRUE(rcp_server_endpoint_submit(&ep, frame.data, frame.len, NULL));
        busy_wait_ms(10u);
        elapsed_ms += 10;
        overflowed = rcp_watchdog_keeper_status(k, 7u).overflowed;
    }
    TEST_ASSERT_TRUE(overflowed);
    TEST_ASSERT_TRUE(rcp_watchdog_keeper_status(k, 7u).enter_safe_state);

    rcp_watchdog_keeper_destroy(k);
    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_transition_now_requires_idle_for_demotion);
    RUN_TEST(test_transition_now_rejects_unauthorized_writer);
    RUN_TEST(test_hw_generic_covers_ep_generic_and_queue_config_with_locked_response);
    RUN_TEST(test_hw_configured_admits_only_ep0);
    RUN_TEST(test_admit_takes_no_lifecycle_state_or_stream_identity);
    RUN_TEST(test_rcp_cfg_inconsistent_catches_an_orphaned_stream);
    RUN_TEST(test_rcp_cfg_inconsistent_a_bound_endpoint_satisfies_bullet_two);
    RUN_TEST(test_rcp_cfg_inconsistent_catches_the_specific_orphaned_stream_among_several);
    RUN_TEST(test_rcp_cfg_inconsistent_response_stream_index_out_of_range);
    RUN_TEST(test_rcp_cfg_consistent_response_stream_index_within_range);
    RUN_TEST(test_rcp_cfg_inconsistent_ignores_stream_index_without_stream_assoc);
    RUN_TEST(test_rcp_cfg_inconsistent_an_unused_endpoint_does_not_cover_a_stream);
    RUN_TEST(test_hw_unconfigured_admission_ignores_claimant_but_writes_still_gated);
    RUN_TEST(test_hw_configured_write_access_now_requires_unicast_and_authorization);
    RUN_TEST(test_hw_configured_drops_tscf);
    RUN_TEST(test_hw_configured_rejects_gbb_addressed_to_ep0);
    RUN_TEST(test_discovery_write_authority_survives_rcp_configured);

    RUN_TEST(test_cold_start_target_recovers_the_configured_lifecycle_state);
    RUN_TEST(test_standby_is_classified_hot_so_configuration_is_retained);
    RUN_TEST(test_hotstart_now_has_network_check_and_records_responder_stream);
    RUN_TEST(test_wakeup_repetition_ignores_other_valid_avtpdus);
    RUN_TEST(test_network_wake_now_requires_the_same_handshake_as_pin);
    RUN_TEST(test_sleep_entry_is_request_only_with_no_network_path);
    RUN_TEST(test_network_sleep_cannot_be_wired_to_standby);
    RUN_TEST(test_network_sleep_applies_the_same_conditions_as_a_normal_request);
    RUN_TEST(test_sleep_request_moves_one_endpoint_only);
    RUN_TEST(test_sleepcmd_requires_root_client_authorization);
    RUN_TEST(test_commit_entry_re_checks_the_gate_and_aborts_the_race);
    RUN_TEST(test_commit_entry_requires_the_response_to_have_been_sent);
    RUN_TEST(test_network_sleep_refusal_return_value_gates_the_lps_confirmation);
    RUN_TEST(test_entry_gate_is_scoped_to_one_endpoint_and_one_queue);
    RUN_TEST(test_admission_is_suspended_during_the_sleep_drain);

    RUN_TEST(test_disabled_endpoint_executes_config_requests_immediately);
    RUN_TEST(test_disabled_endpoint_still_queues_operational_and_compound_wait_requests);
    RUN_TEST(test_disabled_endpoint_queuing_emits_requested_acknowledge);
    RUN_TEST(test_response_queue_flush_period_is_carried_but_inert);
    RUN_TEST(test_gptp_trigger_evaluate_derives_signal_and_composes_with_notify);
    RUN_TEST(test_gptp_trigger_evaluate_first_observation_never_an_edge);

    RUN_TEST(test_sequencer_zero_state_disables_start_condition_and_advance);
    RUN_TEST(test_sequencer_zero_state_ownership_and_regmap_wiring);
    RUN_TEST(test_ntscf_standard_request_still_executes_immediately);
    RUN_TEST(test_tscf_standard_request_postponed_until_presentation_time);
    RUN_TEST(test_tscf_conditional_request_needs_both_its_own_condition_and_the_gate);
    RUN_TEST(test_tscf_pending_standard_request_emits_requested_acknowledge);
    RUN_TEST(test_tscf_pending_standard_request_no_acknowledge_when_evt3_clear);
    RUN_TEST(test_timed_encode_request_tscf_produces_a_real_abb_message);
    RUN_TEST(test_watchdog_overflows_despite_continuous_requests);

    return UNITY_END();
}
