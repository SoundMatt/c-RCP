/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:test REQ-MOCK-021
//cfusa:test REQ-MOCK-022
//cfusa:test REQ-MOCK-023
//cfusa:test REQ-MOCK-024
//cfusa:test REQ-MOCK-025
//cfusa:test REQ-MOCK-026
//cfusa:test REQ-MOCK-027
//cfusa:test REQ-SRV-019
//cfusa:test REQ-SRV-020
//cfusa:test REQ-SRV-021
//cfusa:test REQ-SRV-022
//cfusa:test REQ-ACF-024
//cfusa:test REQ-ACF-031
//cfusa:test REQ-ACF-033
//cfusa:test REQ-MOCK-028
//cfusa:test REQ-MOCK-029
//cfusa:test REQ-CANCEL-012
//cfusa:test REQ-TIMED-012
//cfusa:test REQ-TIMED-013
//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
//cfusa:test REQ-WIREERR-005
//cfusa:test REQ-WIREERR-006
/*
 * test_conditional_dispatch.c -- end-to-end tests for conditional-request
 * dispatch (TC18 §11.2.2/§11.2.3) through the real server path.
 *
 * Every other test file in this repo that touches a conditional request
 * exercises one module's encode/decode/predicate functions in isolation.
 * That is exactly how the feature came to be shipped un-wired: each
 * module was individually correct and individually tested, while nothing
 * in src/mock.c or src/server.c ever looked at a request_type at all, so
 * a compound request bound to a sequencer state and a plain standard
 * request were dispatched identically and unconditionally.
 *
 * These tests therefore all go through rcp_mock_server_dispatch() /
 * rcp_mock_server_dispatch_frame() and rcp_mock_server_tick() -- the
 * actual reference-server entry points -- and assert on what the
 * endpoint's registered handler actually saw and when. A test here fails
 * if a request kind is stored but never executes, executes when its
 * condition is not met, or executes in the wrong order relative to
 * another kind.
 */
#include "unity.h"

#include "../src/mem_bounded.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/request.h>
#include <rcp/request_sequencer.h>
#include <rcp/scheduler.h>
#include <rcp/server.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const rcp_lifecycle_plausibility_snapshot_t EMPTY_SNAP = {NULL, 0, NULL, 0};

/* ── A handler that records every request it is actually given ────────────── */

#define MAX_SEEN 16

typedef struct {
    size_t  count;
    /* The opcode octet of each executed request, so a test can tell which
     * stored request actually ran (and in what order). */
    uint8_t opcode[MAX_SEEN];
    uint8_t transaction_num[MAX_SEEN];
} handler_log_t;

static void logging_handler(const uint8_t *request, size_t request_len, rcp_bytes_t *out_response,
                             void *user_data)
{
    handler_log_t *log = (handler_log_t *)user_data;
    uint8_t        opcode = 0;

    (void)out_response;
    if (log->count >= MAX_SEEN) return;

    if (request && request_len > RCP_ACF_ABB_HEADER_LEN) {
        opcode = request[RCP_ACF_ABB_HEADER_LEN];
        log->transaction_num[log->count] = request[5]; /* Table 4 octet 5 */
    }
    log->opcode[log->count] = opcode;
    log->count++;
}

/* A server with one enabled endpoint at byte_bus_id 1, a 4-register
 * sequencer table, and the logging handler attached.
 *
 * RCP_CONFIGURED, not HW_CONFIGURED: as of the REQ-LIFECYCLE-032 fix,
 * HW_CONFIGURED admits only requests to EP0 (byte_bus_id 0) -- this
 * fixture's whole point is dispatching conditional requests to byte_bus_id
 * 1, which needs the fully-operational state. EMPTY_SNAP's zero endpoint/
 * request-stream counts trivially satisfy both plausibility checks along
 * the way, the same technique already used to reach HW_CONFIGURED below. */
static rcp_mock_server_t *fixture(handler_log_t *log)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_lifecycle_writer_ctx_t none = {0}; /* not consulted for HW_UNCONFIGURED -> HW_CONFIGURED */
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false}; /* REQ-LIFECYCLE-031:
                                                                       required for the
                                                                       RCP_CONFIGURED advance */
    rcp_regmap_request_stream_cfg_t stream_cfg[1];
    uint16_t                        i;

    memset(log, 0, sizeof(*log));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP, none, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint(srv, 1, 1, true /* ep_enable */, logging_handler, log));
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 4));

    /* REQ-SEQ-013 (issue #335): submit()'s own dispatch calls all use
     * stream_id=1u; claim every sequencer as owned by that same stream
     * (resolves to request_stream_index 1) so this fixture's own
     * compound/compound-wait tests -- none of which are themselves
     * exercising REQ-SEQ-013's own access control -- aren't newly
     * blocked by the fail-closed default an unclaimed sequencer now
     * gets. Tests that DO exercise the access-control gate build their
     * own fixture instead, deliberately leaving a sequencer unclaimed or
     * owned by a different client (see the dedicated ownership tests
     * below). */
    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = 1u;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(rcp_sequencer_set_owner(rcp_mock_server_sequencers(srv), i, 1u));
    }

    return srv;
}

static rcp_mock_dispatch_result_t submit(rcp_mock_server_t *srv, const rcp_bytes_t *frame)
{
    rcp_bytes_t                resp = {0};
    rcp_mock_dispatch_result_t r;

    r = rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, true, 1u,
                                  frame->data, frame->len, &resp);
    rcp_bytes_free(&resp);
    return r;
}

/* submit()'s own byte_bus_id-parameterized sibling -- fixture()'s own
 * stream_id (1u) is unchanged, only the addressed endpoint varies, for
 * the cross-endpoint broadcast tests below (issue #335), which need a
 * second endpoint on the same stream. */
static rcp_mock_dispatch_result_t submit_to(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                             const rcp_bytes_t *frame)
{
    rcp_bytes_t                resp = {0};
    rcp_mock_dispatch_result_t r;

    r = rcp_mock_server_dispatch(srv, byte_bus_id, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB,
                                  true, 1u, frame->data, frame->len, &resp);
    rcp_bytes_free(&resp);
    return r;
}

/* submit()'s own TSCF-headed sibling (REQ-TIMED-012/013, issue #422):
 * threads tv/avtp_timestamp/gptp_reference_now through to
 * rcp_mock_server_dispatch_tscf() instead of the plain NTSCF entry point
 * submit() uses, for the TSCF cancellation presentation-time gate tests
 * below. Same byte_bus_id (1)/stream_id (1u)/ACF_MSG_TYPE_GBB convention
 * as submit() itself -- every frame this file dispatches carries a
 * repurposed opcode byte. */
static rcp_mock_dispatch_result_t submit_tscf(rcp_mock_server_t *srv, const rcp_bytes_t *frame,
                                               bool tv, uint32_t avtp_timestamp,
                                               uint64_t gptp_reference_now)
{
    rcp_bytes_t                resp = {0};
    rcp_mock_dispatch_result_t r;

    r = rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_GBB, true, 1u,
                                       tv, avtp_timestamp, false, gptp_reference_now, frame->data,
                                       frame->len, &resp);
    rcp_bytes_free(&resp);
    return r;
}

/* A tick context with nothing satisfied: idle endpoint, gPTP locked, not
 * in safe state, no compound-wait match. Tests adjust what they need. */
static rcp_server_tick_ctx_t base_ctx(uint32_t now)
{
    rcp_server_tick_ctx_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.now                 = now;
    ctx.gptp_now            = 0;
    ctx.gptp_locked         = true;
    ctx.sequencers          = NULL; /* overwritten by rcp_mock_server_tick() */
    ctx.endpoint_idle       = true;
    ctx.in_safe_state       = false;
    ctx.current_status      = NULL; /* no status -> no COMPOUND_WAIT ever matches */
    ctx.current_status_len  = 0;
    return ctx;
}

static bool tick(rcp_mock_server_t *srv, const rcp_server_tick_ctx_t *ctx)
{
    rcp_bytes_t resp = {0};
    bool        ran  = rcp_mock_server_tick(srv, 1, ctx, &resp);

    rcp_bytes_free(&resp);
    return ran;
}

/* ── Standard requests are unaffected ─────────────────────────────────────── */

/* The whole routing change must be invisible to a plain, non-repurposed
 * request: it still executes immediately, exactly as before. */
static void test_standard_request_still_executes_immediately(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t info = {0};

    info.byte_bus_id     = 1;
    info.transaction_num = 3;
    frame = rcp_acf_encode_abb(&info, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Compound: sequencer-state gated execution ────────────────────────────── */

static rcp_bytes_t make_compound(uint8_t request_type, uint8_t seq, uint8_t start, uint8_t next,
                                  uint16_t delay, uint16_t repeats, uint8_t txn)
{
    rcp_compound_step_t step = {0};

    step.start_state     = start;
    step.next_state       = next;
    step.sequencer_index  = seq;
    step.exec_delay       = delay;
    step.repeat_count     = repeats;
    /* evt is irrelevant to every caller of this helper (none are
     * COMPOUND_WAIT); see test_compound_wait_requires_the_wait_condition()
     * for a COMPOUND_WAIT request built with a real evt/payload. */
    return rcp_compound_encode_request(request_type, 1, &step, 0u, txn, NULL, 0);
}

//cfusa:test REQ-MOCK-027
//cfusa:test REQ-SRV-021
static void test_compound_waits_for_its_sequencer_state(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* Sequencer 2 must reach state 5 before this executes; power-on state
     * is RCP_SEQUENCER_POWER_ON_STATE (1), so it starts unsatisfied. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 2, 5, 9, 0, 0, 11);
    rcp_server_tick_ctx_t ctx = base_ctx(0);
    uint8_t got = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, log.count); /* stored, NOT executed */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    /* Condition unmet: ticking does nothing, however many times. */
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* Drive the sequencer into start_state: now it runs. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(rcp_mock_server_sequencers(srv), 2, 5));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND, log.opcode[0]);

    /* And it advanced its sequencer to next_state, then left the store. */
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(rcp_mock_server_sequencers(srv), 2, &got));
    TEST_ASSERT_EQUAL_UINT8(9, got);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Table 26's Compound row: "the state advances to RE as soon as the EP
 * is idle" -- the same gate test_triggered_never_fires_while_endpoint_busy()
 * exercises for Triggered. REQ-SRV-006. */
//cfusa:test REQ-SRV-006
static void test_compound_never_fires_while_endpoint_busy(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* start_state 1 == the power-on state, so the start condition holds
     * from admission and the (zero) delay is already elapsed -- only
     * ctx.endpoint_idle stands in the way. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 4, 0, 0, 13);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    ctx.endpoint_idle = false;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.endpoint_idle = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

static void test_compound_exec_delay_holds_execution_back(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* start_state 1 == the power-on state, so the start condition holds
     * from admission; only the delay stands in the way. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 4, 100, 0, 12);
    rcp_server_tick_ctx_t ctx;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* Arming happens on the first tick whose start condition holds; the
     * exec_delay then runs from there. */
    ctx = base_ctx(1000);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    ctx = base_ctx(1099);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx = base_ctx(1100);
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Compound-wait: the caller-supplied comparison result ─────────────────── */

static void test_compound_wait_requires_the_wait_condition(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;
    /* TC18 §13.5.1 evt[2:0] = 000b (exact match) against this 2-byte
     * byte_msg_payload -- the real comparison mode/target this request
     * carries on the wire, not a caller-injected shortcut. */
    const uint8_t target[2] = {0x01, 0x02};
    const uint8_t mismatched_status[2] = {0x01, 0x03};
    const uint8_t matching_status[2]   = {0x01, 0x02};
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    step.start_state    = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state      = 7;
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step, 0x0u, 13,
                                         target, sizeof(target));
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* Sequencer is in start_state, but the endpoint's current status
     * does not match this request's own byte_msg_payload. */
    ctx.current_status     = mismatched_status;
    ctx.current_status_len = sizeof(mismatched_status);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.current_status     = matching_status;
    ctx.current_status_len = sizeof(matching_status);
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND_WAIT, log.opcode[0]);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Two pending COMPOUND_WAIT requests on the same endpoint, each with its
 * own independent byte_msg_payload target, must be evaluated
 * independently against the shared current status -- a single flat
 * "the" wait condition cannot represent this. */
static void test_two_pending_compound_waits_have_independent_targets(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_compound_step_t step_a = {0};
    rcp_compound_step_t step_b = {0};
    rcp_bytes_t frame_a, frame_b;
    const uint8_t target_a[2] = {0x00, 0x05};
    const uint8_t target_b[2] = {0x00, 0x09};
    const uint8_t status_matches_only_a[2] = {0x00, 0x05};
    const uint8_t status_matches_only_b[2] = {0x00, 0x09};
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    /* Distinct sequencers, so one executing never disturbs the other's
     * own start condition. */
    step_a.sequencer_index = 0;
    step_a.start_state      = RCP_SEQUENCER_POWER_ON_STATE;
    step_a.next_state        = 1;
    step_b.sequencer_index = 1;
    step_b.start_state      = RCP_SEQUENCER_POWER_ON_STATE;
    step_b.next_state        = 1;

    frame_a = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step_a, 0x0u, 21,
                                           target_a, sizeof(target_a));
    frame_b = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step_b, 0x0u, 22,
                                           target_b, sizeof(target_b));
    TEST_ASSERT_NOT_NULL(frame_a.data);
    TEST_ASSERT_NOT_NULL(frame_b.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame_a));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame_b));
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    /* Status matches only A's target: exactly A runs, never B -- a shared
     * flat condition_met could not tell these two requests apart. */
    ctx.current_status     = status_matches_only_a;
    ctx.current_status_len = sizeof(status_matches_only_a);
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(21, log.transaction_num[0]);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1)); /* B still pending */

    /* Now status matches only B's (still-pending) target. */
    ctx.current_status     = status_matches_only_b;
    ctx.current_status_len = sizeof(status_matches_only_b);
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(2, log.count);
    TEST_ASSERT_EQUAL_HEX8(22, log.transaction_num[1]);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame_a);
    rcp_bytes_free(&frame_b);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-SEQ-013: compound/compound-wait admission's own sequencer-
 *    ownership access control ───────────────────────────────────────────── */

//cfusa:test REQ-SEQ-013
static void test_compound_admission_denied_for_unclaimed_sequencer(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log); /* claims sequencers 0-3 for stream_id=1 */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0, RCP_SEQUENCER_POWER_ON_STATE,
                                       1, 0, 0, 31);

    /* Release sequencer 0 back to unclaimed -- fixture()'s own claim
     * undone for this one test only. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(rcp_mock_server_sequencers(srv), 0,
                                              RCP_SEQUENCER_OWNER_UNCLAIMED));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1)); /* never admitted */

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SEQ-013
static void test_compound_admission_denied_for_sequencer_owned_by_a_different_client(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log); /* claims sequencers 0-3 for stream_id=1 */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0, RCP_SEQUENCER_POWER_ON_STATE,
                                       1, 0, 0, 32);

    /* Reassign sequencer 0 to a different client (99) -- submit()'s own
     * dispatch calls all resolve to request_stream_index 1, so this
     * request is no longer the recorded owner. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(rcp_mock_server_sequencers(srv), 0, 99u));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1)); /* never admitted */

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* REQ-WIREERR-005 (issue #163): a sequencer_index that isn't even a real
 * entry in this server's table (fixture()'s own table has 4: 0-3) is a
 * DIFFERENT TC18 Table 30 rejection reason than "owned by someone else"
 * -- SEQUENCER_NOT_KNOWN (2), not UNAUTHORIZED_ACCESS (3) -- and, unlike
 * the two REJECTED-only tests just above, this test checks the real
 * wire bytes of the Error Response, not just the dispatch-result enum,
 * the same "prove the numbered code, not just the rejection" standard
 * test_compound_wait_reserved_evt_sends_err_response() already
 * established for RCP_ERROR_UNSUPPORTED_CMD. */
//cfusa:test REQ-WIREERR-005
static void test_compound_admission_rejected_for_unknown_sequencer_index_reports_sequencer_not_known(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log); /* 4-register table: indices 0-3 valid */
    rcp_bytes_t                  frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 9,
                                                        RCP_SEQUENCER_POWER_ON_STATE, 1, 0, 0, 41);
    rcp_bytes_t                  resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1)); /* never admitted */
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(41u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_SEQUENCER_NOT_KNOWN, payload[0]);

    rcp_bytes_free(&frame);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SEQ-013
static void test_compound_wait_admission_denied_for_unclaimed_sequencer(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    const uint8_t target[2] = {0x00, 0x05};
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;

    step.sequencer_index = 1;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state      = 1;

    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(rcp_mock_server_sequencers(srv), 1,
                                              RCP_SEQUENCER_OWNER_UNCLAIMED));

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step, 0x0u, 33,
                                         target, sizeof(target));
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SEQ-013
static void test_compound_admission_permitted_when_no_sequencer_table_configured(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0, RCP_SEQUENCER_POWER_ON_STATE,
                                       1, 0, 0, 34);

    /* REQ-SEQ-013's own ownership gate is deliberately skipped when the
     * sequencer table itself is unsupported (count == 0) -- a distinct,
     * pre-existing "compound operations unsupported entirely" scenario,
     * not this requirement's own concern (see dispatch_plain()'s own
     * doc comment, mock.c). Still admitted PENDING, exactly like
     * test_compound_never_due_without_a_sequencer_table(). */
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 0));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* TC18 §13.5.1: evt[2:0] = 011b is reserved for a compound-wait request --
 * "request shall be ignored and an err-response with error code =
 * UNSUPPORTED_CMD shall be sent". Admission must reject it outright
 * rather than storing it as a request that can simply never match. */
static void test_compound_wait_reserved_evt_is_rejected_at_admission(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;

    step.start_state = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state    = 1;
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step, 0x3u, 30,
                                         NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* TC18 §13.5.1, not §11.3.1 (issue #454, extending REQ-ACF-024/
 * REQ-ACF-033): a reserved compound-wait evt is rejected by
 * rcp_server_endpoint_admit() itself (RCP_SERVER_ADMIT_REJECTED,
 * *out_error = RCP_ERROR_UNSUPPORTED_CMD) BEFORE the request is ever
 * filed into EP request storage -- but §13.5.1's own text for exactly
 * this rejection reason overrides the general §11.3.1 "never filed"
 * default (which #430/REQ-ACF-033 correctly established for every OTHER
 * admission-rejection reason): "evt[2:0] = 011b - reserved - request
 * shall be ignored and an err-response with error code = UNSUPPORTED_CMD
 * shall be sent." "err-response" is TC18's own specific term for the
 * §11.3.4 Error Response shape (evt[3:0] < 0x9, err = 1) -- structurally
 * distinct from §11.3.1's Acknowledge (evt[3:0] = 0xF) -- and it appears
 * nowhere else in the specification for this admission path.
 *
 * FIXED 2026-08-14 (issue #454, c-RCP-AUDIT-29): the #430 fix
 * over-generalized and rewrote this test (previously named
 * test_compound_wait_reserved_evt_sends_acknowledge_rejected_response())
 * to pin the Acknowledge shape here, which was itself a regression from
 * the correct, pre-#430 §11.3.4 shape. finish_admission()'s REJECTED
 * case (src/mock.c) now looks up the actual TC18-mandated shape per
 * rejection reason (admission_reject_response_shape()) instead of
 * treating every rejection uniformly, and calls
 * rcp_acf_build_error_response() for this one specific reason. Still
 * checks the actual wire bytes, not just the dispatch-result enum. */
static void test_compound_wait_reserved_evt_sends_err_response(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_compound_step_t          step = {0};
    rcp_bytes_t                  frame, resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    /* fixture()'s own default stream_cfg (rx_resp_stream_index left at
     * its TC18-defined power-on default of 1, "no configuration needed
     * to observe a response") is enough here: the §11.3.4 Error Response
     * shape is governed by rx_resp_stream_index, unlike the §11.3.1
     * Acknowledge shape (see suppress_response_per_stream_cfg(),
     * REQ-RMAP-048/049), so -- unlike the old, misclassified test this
     * one replaces -- no extra stream_cfg override is needed to see this
     * response on the wire. */
    step.start_state = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state    = 1;
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step, 0x3u, 30, NULL,
                                         0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_EVT_ACKNOWLEDGE, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(30u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_UNSUPPORTED_CMD, payload[0]);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* rcp_timed_encode_request()'s own evt-controllable sibling: that public
 * encoder hardcodes evt=0 (no acknowledge requested), which cannot
 * exercise finish_admission()'s new evt[3] "if requested" gate on the
 * §11.3.1 Acknowledge-rejected shape (issue #454). Builds an identical
 * wire frame by hand, mirroring rcp_timed_encode_request()'s own
 * construction (src/request_timed.c) with no payload, but with a
 * caller-controlled evt nibble. */
static rcp_bytes_t make_timed_with_evt(rcp_byte_bus_id_t byte_bus_id, uint64_t presentation_time,
                                        uint8_t transaction_num, uint8_t evt)
{
    rcp_acf_byte_message_info_t info = {0};
    uint8_t                     b[RCP_ACF_GBB_HEADER_LEN];
    uint64_t                    ts;
    int                         i;

    info.byte_bus_id     = byte_bus_id;
    info.transaction_num = transaction_num;
    info.evt              = evt;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, (uint16_t)(RCP_ACF_GBB_HEADER_LEN / 4u), &info);

    ts = (((uint64_t)RCP_REQUEST_TYPE_TIMED) << 56) |
         (presentation_time & RCP_TIMED_PRESENTATION_TIME_MAX);
    for (i = 0; i < 8; i++) {
        b[RCP_ACF_ABB_HEADER_LEN + (size_t)i] = (uint8_t)(ts >> (56 - 8 * i));
    }

    return rcp_bytes_dup(b, RCP_ACF_GBB_HEADER_LEN);
}

/* issue #454: the §11.3.1 Acknowledge-rejected shape's evt[3] "if
 * requested" gate, mirroring rcp_server_endpoint_submit()'s own
 * REQ-SRV-016 discipline for the success-Acknowledge sibling ("Both
 * success and rejected Acknowledge are the same §11.3.1 response type
 * per Table 16, so the same 'if requested' gating should logically apply
 * to both" -- issue #454). Drives a genuine "never filed" rejection
 * (RCP_ERROR_REQUEST_STORAGE_OVERFLOW: the request store is full, TC18
 * §12.7's own wording for this case carries no "err-response" override,
 * so it correctly keeps the §11.3.1 shape) with evt[3] set -- the
 * response is built. */
static void test_admission_rejection_acknowledge_sent_when_evt3_requests_it(void)
{
    handler_log_t       log;
    rcp_mock_server_t   *srv = fixture(&log);
    rcp_bytes_t          frame, resp = {0};
    rcp_acf_byte_message_info_t hdr;
    const uint8_t        *payload;
    size_t                payload_len;
    size_t                i;

    /* fixture()'s own default rx_ack_stream_index (0, TC18's own "no
     * acknowledge is to be sent" power-on default) would suppress this
     * response before it ever reached this test -- see
     * suppress_response_per_stream_cfg() (REQ-RMAP-048) -- so it must be
     * configured to a real stream for the §11.3.1 Acknowledge shape to
     * be observable, exactly as TC18 itself requires. */
    {
        rcp_regmap_request_stream_cfg_t stream_cfg[1];

        rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
        stream_cfg[0].rx_stream_id        = 1u;
        stream_cfg[0].rx_ack_stream_index = 1u;
        TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));
    }

    /* Fill the request store to capacity so the next admission genuinely
     * overflows (RCP_ERROR_REQUEST_STORAGE_OVERFLOW), the same technique
     * test_overflow_latches_stream_status() above already establishes. */
    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        frame = rcp_timed_encode_request(1, 0x1000u + (uint64_t)i, (uint8_t)i, NULL, 0u);
        TEST_ASSERT_NOT_NULL(frame.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
        rcp_bytes_free(&frame);
    }

    /* The overflowing request itself asks for an acknowledge (evt[3]=1). */
    frame = make_timed_with_evt(1, 0x9000u, 77u, 0x08u);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_EVT_ACKNOWLEDGE, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(77u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_STORAGE_OVERFLOW, payload[0]);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* issue #454's other half: the identical overflow scenario, but the
 * overflowing request's own evt[3] does NOT request an acknowledge --
 * finish_admission() must build no response at all, exactly as
 * rcp_server_endpoint_submit()'s own REQ-SRV-016 gate leaves *out_ack
 * zeroed when evt[3] is clear. This is the regression guard for #430's
 * own original, correct scope: a genuine "never filed" rejection with no
 * "err-response" wording anywhere in TC18 still uses the §11.3.1
 * Acknowledge shape's own gating discipline, not the unconditional
 * §11.3.4 Error Response path. */
static void test_admission_rejection_acknowledge_suppressed_when_evt3_not_requested(void)
{
    handler_log_t       log;
    rcp_mock_server_t   *srv = fixture(&log);
    rcp_bytes_t          frame, resp = {0};
    size_t                i;
    rcp_regmap_request_stream_cfg_t stream_cfg[1];

    /* rx_ack_stream_index configured to a real stream too, so a failure
     * of the evt[3] gate itself (not stream suppression) is what this
     * test would actually catch. */
    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id        = 1u;
    stream_cfg[0].rx_ack_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        frame = rcp_timed_encode_request(1, 0x1000u + (uint64_t)i, (uint8_t)i, NULL, 0u);
        TEST_ASSERT_NOT_NULL(frame.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
        rcp_bytes_free(&frame);
    }

    /* evt=0x00: evt[3] clear, no acknowledge requested. */
    frame = make_timed_with_evt(1, 0x9000u, 78u, 0x00u);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── issue #463 (REQ-SRV-016): admit()'s own success-path Acknowledge ────────
 *
 * TC18 §12.9.5's own generic wording -- "an acknowledge is given if
 * requested as soon as the new request has been successfully queued for
 * execution in the addressed endpoint's request storage" -- is worded over
 * the whole endpoint request storage, not scoped to a Standard request the
 * way rcp_server_endpoint_submit()'s own REQ-SRV-016 fix (issue #201)
 * reads. rcp_server_endpoint_admit() itself (server.c) never built one for
 * a Compound/Compound-Wait/Triggered/Timed/Chained request placed into
 * ep->pending -- this is the success-path sibling of issue #454's own
 * rejection-path fix immediately above: same evt[3] "if requested" gate,
 * same §11.3.1 Acknowledge wire shape, but err=0 (accepted, not rejected)
 * this time. Timed is used here (make_timed_with_evt(), above) for the
 * identical evt-control reason #454's own tests already established. */
static void test_pending_conditional_request_emits_requested_acknowledge(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_bytes_t                  frame, resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;
    rcp_regmap_request_stream_cfg_t stream_cfg[1];

    /* fixture()'s own default rx_ack_stream_index (0) would suppress this
     * response before it ever reached this test -- see
     * suppress_response_per_stream_cfg() -- so it must be configured to a
     * real stream first, exactly as issue #454's own rejection-Acknowledge
     * tests above already do. */
    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id        = 1u;
    stream_cfg[0].rx_ack_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    /* evt[3] = 1: this admission-succeeding request asks for an ack. */
    frame = make_timed_with_evt(1, 0x9000u, 88u, 0x08u);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_EVT_ACKNOWLEDGE, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(0u, hdr.err); /* success -- NOT #454's rejection shape */
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(88u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);
    /* The request really was filed, not merely acknowledged -- the same
     * "still stored" proof #454's own overflow-fill loop relies on. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* evt[3] clear: the identical successful admission, but no acknowledge is
 * built -- REQ-SRV-016's own "if requested" conditional wording, matching
 * rcp_server_endpoint_submit()'s existing behavior for the plain
 * Standard-queuing case and issue #454's rejection-Acknowledge sibling
 * test's own identical evt[3]=0 shape. */
static void test_pending_conditional_request_no_acknowledge_when_evt3_clear(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_bytes_t                  frame, resp = {0};
    rcp_regmap_request_stream_cfg_t stream_cfg[1];

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id        = 1u;
    stream_cfg[0].rx_ack_stream_index = 1u;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    frame = make_timed_with_evt(1, 0x9000u, 89u, 0x00u);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Triggered: the real trigger-selection mechanism ──────────────────────── */

static rcp_bytes_t make_triggered(uint8_t request_type, uint8_t source_ep, uint8_t signal_nr,
                                   uint8_t threshold, uint16_t repeats, uint8_t txn)
{
    rcp_triggered_step_t step = {0};

    step.trigger_source_ep = source_ep;
    step.trigger_signal_nr = signal_nr;
    step.trigger_threshold = threshold;
    step.exec_delay         = 0;
    step.repeat_count       = repeats;
    return rcp_triggered_encode_request(request_type, 1, &step, txn, NULL, 0);
}

static void test_triggered_executes_only_on_its_own_trigger(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* Waits on endpoint 6's trigger signal 2, threshold 0 (fires on the
     * first occurrence). */
    rcp_bytes_t frame = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 2, 0, 0, 21);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
    TEST_ASSERT_FALSE(tick(srv, &ctx));

    /* Wrong endpoint, and wrong signal number of the right endpoint:
     * neither counts, so the request stays put. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_trigger(srv, 5, 2));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_trigger(srv, 6, 3));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* The trigger it actually named. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 6, 2));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TRIGGERED, log.opcode[0]);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Table 8: a threshold of 3 executes after four occurrences. */
static void test_triggered_threshold_delays_execution(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 2, 3, 0, 22);
    rcp_server_tick_ctx_t ctx = base_ctx(0);
    int i;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 6, 2));
        TEST_ASSERT_FALSE(tick(srv, &ctx));
    }
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 6, 2));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

static void test_triggered_never_fires_while_endpoint_busy(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, 0, 0, 0, 23);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 0, 0));

    ctx.endpoint_idle = false;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.endpoint_idle = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-SRV-018: the gPTP lock trigger arms via the same real mechanism ───── */

/* rcp_mock_server_notify_gptp_lock_state()'s own edge-derived signal
 * (RCP_SERVER_GPTP_TRIGGER_ESTABLISHED, Table 37 signal 0) arms a stored
 * Triggered request exactly as rcp_mock_server_notify_trigger() itself
 * already does above -- proven by literally the same
 * fixture/submit/tick sequence, source_ep standing in for "this
 * deployment's own convention for the RC Server as a trigger source". */
static void test_gptp_lock_established_arms_a_waiting_triggered_request(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* Waits on endpoint 0's (the RC Server's own convention here) trigger
     * signal 0 (RCP_SERVER_GPTP_TRIGGER_ESTABLISHED), threshold 0. */
    rcp_bytes_t frame =
        make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, RCP_SERVER_GPTP_TRIGGER_ESTABLISHED, 0, 0, 41);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* The very first observation is never itself an edge -- nothing to
     * notify, nothing arms. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, false, 0));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* unlocked -> locked: a genuine edge, signal 0. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_gptp_lock_state(srv, true, 0));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TRIGGERED, log.opcode[0]);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* A request waiting on signal 1 (LOST) is untouched by an ESTABLISHED
 * edge, and vice versa -- REQ-SRV-018's own two distinct signals are not
 * conflated by this wiring. */
static void test_gptp_lock_signals_are_not_conflated(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame =
        make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, RCP_SERVER_GPTP_TRIGGER_LOST, 0, 0, 42);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* unlocked -> locked fires ESTABLISHED (signal 0), not LOST (signal
     * 1) -- this request, waiting on signal 1, stays put. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, false, 0));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, true, 0));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* locked -> unlocked fires LOST (signal 1): the request it actually
     * named. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_gptp_lock_state(srv, false, 0));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* source_ep is threaded through unchanged, in both directions: a
 * request naming a DIFFERENT trigger_source_ep than the caller's own
 * gPTP-source convention is not armed (the same "wrong endpoint, no
 * match" discipline rcp_mock_server_notify_trigger() itself already
 * enforces), and reporting the MATCHING source_ep does arm it -- proving
 * this is a real passthrough, not a hardcoded value that happens not to
 * matter for the mismatched case alone. */
static void test_gptp_lock_state_respects_source_ep(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame =
        make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, RCP_SERVER_GPTP_TRIGGER_ESTABLISHED, 0, 0, 43);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, false, 0));
    /* unlocked->locked: ESTABLISHED, reported as source_ep=0, but this
     * request waits on source_ep=6 -- no match, nothing arms. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, true, 0));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* locked->unlocked: LOST, reported as source_ep=6 -- source_ep now
     * matches, but the SIGNAL doesn't (this request waits on
     * ESTABLISHED, not LOST) -- still nothing arms. Both dimensions
     * matter independently. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_notify_gptp_lock_state(srv, false, 6));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    /* unlocked->locked again: ESTABLISHED, reported as source_ep=6 --
     * NOW both source_ep and signal_nr match. Proves source_ep is
     * genuinely threaded through to rcp_mock_server_notify_trigger(),
     * not silently dropped or hardcoded. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_gptp_lock_state(srv, true, 6));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Timed: presentation-time gated execution ─────────────────────────────── */

static void test_timed_waits_for_its_presentation_time(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* A presentation_time above 2^32, which the pre-v0.102.0 32-bit
     * encoding could not even represent. */
    const uint64_t pt = 0x0000A5A500000064ull;
    rcp_bytes_t frame = rcp_timed_encode_request(1, pt, 31, NULL, 0);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    ctx.gptp_now = pt - 1u;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.gptp_now = pt;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TIMED, log.opcode[0]);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Without a locked gPTP time base a presentation_time cannot be evaluated
 * at all, so a timed request never becomes due. */
static void test_timed_never_due_without_gptp_lock(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = rcp_timed_encode_request(1, 100, 32, NULL, 0);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    ctx.gptp_locked = false;
    ctx.gptp_now    = 1000; /* long past the presentation_time */
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.gptp_locked = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* REQ-WIREERR-006 (issue #163): "In case the time synchronization hasn't
 * been established, timed requests... shall be rejected and an error
 * response shall be sent (error code = GPTP_FAIL)." -- a DIFFERENT
 * scenario from test_timed_never_due_without_gptp_lock() just above:
 * that test's own ctx.gptp_locked=false is the TICK-time scheduling
 * signal (rcp_server_tick_ctx_t, consulted only once a Timed request is
 * already stored and its own start condition is being evaluated), so
 * the request is admitted successfully first and then simply never
 * becomes due. This test's own time_sync_supported=false is the
 * ADMISSION-time signal (rcp_mock_server_dispatch()'s own parameter,
 * TC18's real "has gPTP been established" concept -- already threaded
 * through every dispatch entry point for REQ-AVTP-021's own TSCF rule
 * 1) -- the spec's own "shall be rejected" case this fix closes: the
 * request is answered with a real Error Response immediately, at
 * admission, and never reaches the request store at all. */
//cfusa:test REQ-WIREERR-006
static void test_timed_request_rejected_at_admission_when_time_sync_not_supported(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_bytes_t                  frame = rcp_timed_encode_request(1, 100, 55, NULL, 0);
    rcp_bytes_t                  resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, /* time_sync_supported */ false,
                                                1u, frame.data, frame.len, &resp));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1)); /* never queued */
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(55u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_GPTP_FAIL, payload[0]);

    rcp_bytes_free(&frame);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* A non-Timed conditional request (Compound) is unaffected by
 * time_sync_supported=false -- REQ-WIREERR-006's own new gate is scoped
 * to request_type == RCP_REQUEST_TYPE_TIMED only, mirroring the
 * sequencer-ownership gate's own kind-scoping just above. */
//cfusa:test REQ-WIREERR-006
static void test_compound_admission_unaffected_by_time_sync_not_supported(void)
{
    handler_log_t      log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t         frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                              RCP_SEQUENCER_POWER_ON_STATE, 1, 0, 0, 56);
    rcp_bytes_t         resp = {0};

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, /* time_sync_supported */ false,
                                                1u, frame.data, frame.len, &resp));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* ── Safety-tagged (0x8x) requests are gated on the safe state ────────────── */

static void test_safety_tagged_request_waits_for_safe_state(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* Identical to the plain compound request above except for the MSB in
     * request_type. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND_SAFETY, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 4, 0, 0, 41);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* Every other condition is satisfied, but the endpoint has not
     * reached its configured safe state, so it stays queued. */
    ctx.in_safe_state = false;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    ctx.in_safe_state = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND_SAFETY, log.opcode[0]);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Priority ordering across kinds ───────────────────────────────────────── */

/* scheduler.h ranks triggered above timed above compound. With all three
 * simultaneously due, that is the order they must execute in -- even
 * though the compound request arrived first. */
static void test_due_requests_execute_in_priority_order(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t compound = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                          RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 51);
    rcp_bytes_t timed    = rcp_timed_encode_request(1, 0, 52, NULL, 0);
    rcp_bytes_t trig     = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, 0, 0, 0, 53);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(compound.data);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_NOT_NULL(trig.data);

    /* Arrival order is deliberately the reverse of priority order. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &compound));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &timed));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &trig));
    TEST_ASSERT_EQUAL_size_t(3, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 0, 0));

    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));

    TEST_ASSERT_EQUAL_size_t(3, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TRIGGERED, log.opcode[0]); /* rank 5 */
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TIMED, log.opcode[1]);     /* rank 4 */
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND, log.opcode[2]);  /* rank 3 */

    rcp_bytes_free(&compound);
    rcp_bytes_free(&timed);
    rcp_bytes_free(&trig);
    rcp_mock_server_destroy(srv);
}

/* Among two requests of the SAME kind, arrival order decides. */
static void test_equal_rank_requests_execute_in_arrival_order(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t first  = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 61);
    rcp_bytes_t second = make_compound(RCP_REQUEST_TYPE_COMPOUND, 1,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 62);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(first.data);
    TEST_ASSERT_NOT_NULL(second.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &first));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &second));

    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(2, log.count);
    TEST_ASSERT_EQUAL_UINT8(61, log.transaction_num[0]);
    TEST_ASSERT_EQUAL_UINT8(62, log.transaction_num[1]);

    rcp_bytes_free(&first);
    rcp_bytes_free(&second);
    rcp_mock_server_destroy(srv);
}

/* ── Repetitions ──────────────────────────────────────────────────────────── */

static void test_repeat_count_controls_how_often_a_request_runs(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* repeat_count 2: this execution plus two more, then removed. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 2, 71);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_TRUE(tick(srv, &ctx));

    TEST_ASSERT_EQUAL_size_t(3, log.count);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_FALSE(tick(srv, &ctx));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* The all-ones sentinel is never decremented, so the request survives
 * every execution. */
static void test_infinite_repeat_count_is_never_exhausted(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 0, 0,
                                       RCP_COMPOUND_REPEAT_INFINITE, 72);
    rcp_server_tick_ctx_t ctx = base_ctx(0);
    int i;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    for (i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(tick(srv, &ctx));
        TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));
    }
    TEST_ASSERT_EQUAL_size_t(5, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── Cancellation ─────────────────────────────────────────────────────────── */

static void test_clear_all_empties_the_request_store(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t a = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0, 5, 0, 0, 0, 81);
    rcp_bytes_t b = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, 0, 0, 0, 82);
    rcp_bytes_t clear = rcp_cancel_encode_clear_all(1, 83);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &a));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &b));
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED, submit(srv, &clear));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    rcp_bytes_free(&a);
    rcp_bytes_free(&b);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

static void test_clear_single_removes_only_its_target(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t a = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                   RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 91);
    rcp_bytes_t b = make_compound(RCP_REQUEST_TYPE_COMPOUND, 1,
                                   RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 92);
    rcp_bytes_t clear = rcp_cancel_encode_clear_single(1, 91, 93);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &a));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &b));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED, submit(srv, &clear));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    /* The survivor is the one that was NOT named. */
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_UINT8(92, log.transaction_num[0]);

    rcp_bytes_free(&a);
    rcp_bytes_free(&b);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* TC18 §11.2.3.3: "The request initiating the cancellation will create an
 * error response with the error code = REQUEST_NOT_FOUND, when the
 * clear_transaction_num was not found." The response carries the
 * cancellation request's own byte_bus_id/transaction_num (§12.9.6's
 * general rule), not the not-found target's. */
static void test_clear_single_not_found_sends_request_not_found_error(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_bytes_t                  clear = rcp_cancel_encode_clear_single(1, 91, 93);
    rcp_bytes_t                  resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    /* Nothing pending at all -- transaction_num 91 cannot be found. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, 1u, clear.data, clear.len,
                                                &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(93u, hdr.transaction_num); /* the cancel request's own tn, not 91 */
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_NOT_FOUND, payload[0]);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* clear-non-safestate (0x06) removes the ordinary requests and leaves the
 * safety sequence intact. */
static void test_clear_non_safestate_keeps_safety_tagged_requests(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t plain  = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 101);
    rcp_bytes_t safety = make_compound(RCP_REQUEST_TYPE_COMPOUND_SAFETY, 1,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 102);
    rcp_bytes_t clear  = rcp_compound_encode_clear_non_safestate(1, 103);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &plain));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &safety));
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED, submit(srv, &clear));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    ctx.in_safe_state = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND_SAFETY, log.opcode[0]);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&safety);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* ── TSCF presentation-time gate applies to Cancel too (issue #422) ────────── */

/* TC18 §11.2: "There are three basic types of requests... If received
 * under TSCF header all of them shall be executed earliest at the given
 * presentation time" -- Standard, Conditional, AND Cancel, with no
 * carve-out. Before this fix, rcp_server_endpoint_admit() returned
 * RCP_SERVER_ADMIT_CANCELLATION for a clear-all unconditionally, and
 * mock.c applied it synchronously with no reference to tv/avtp_timestamp
 * at all -- so a cancellation admitted under a TSCF header with a future
 * presentation time still ran immediately, cancelling both targets right
 * on receipt. This test proves the opposite: the two TRIGGERED targets
 * (unmet threshold, never notified -- so neither can become due on its
 * own, isolating this test from anything except the cancellation itself)
 * are still both present in the request store, completely untouched,
 * after the clear-all is admitted and after a tick before the
 * presentation time is reached. */
static void test_tscf_cancellation_with_future_presentation_time_is_deferred(void)
{
    handler_log_t          log;
    rcp_mock_server_t     *srv = fixture(&log);
    rcp_bytes_t             a  = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 1, 0, 0, 111);
    rcp_bytes_t             b  = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 2, 0, 0, 112);
    rcp_bytes_t             clear = rcp_cancel_encode_clear_all(1, 113);
    rcp_server_tick_ctx_t   ctx = base_ctx(0);

    TEST_ASSERT_NOT_NULL(a.data);
    TEST_ASSERT_NOT_NULL(b.data);
    TEST_ASSERT_NOT_NULL(clear.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &a));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &b));
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    /* tv=true, avtp_timestamp comfortably ahead of gptp_reference_now
     * (0u): the reconstructed presentation instant is in the future. The
     * cancellation must be PENDING, not CANCELLED -- and, critically,
     * neither target may have been removed from the store yet. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_tscf(srv, &clear, true, 5000000u, 0u));
    TEST_ASSERT_EQUAL_size_t(3, rcp_mock_server_pending_count(srv, 1)); /* a, b, and the cancel itself */

    /* A tick before the presentation time: gPTP locked, endpoint idle,
     * but neither the cancellation's own gate nor either TRIGGERED
     * target's own threshold is satisfied -- nothing runs at all. */
    ctx.gptp_locked = true;
    ctx.gptp_now    = 4999999u;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(3, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_EQUAL_size_t(0, log.count); /* neither target ever ran */

    rcp_bytes_free(&a);
    rcp_bytes_free(&b);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* Companion to the deferral test above: once the reconstructed
 * presentation instant is actually reached (and gPTP is locked), the
 * deferred cancellation becomes due -- ranking above every other kind
 * (scheduler.h's own cancellation > triggered > ... ordering) -- and
 * apply_cancellation() removes both targets, exactly as the immediate
 * (tv=false) path already does. */
static void test_tscf_cancellation_executes_once_presentation_time_passes(void)
{
    handler_log_t          log;
    rcp_mock_server_t     *srv = fixture(&log);
    rcp_bytes_t             a  = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 1, 0, 0, 121);
    rcp_bytes_t             b  = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 2, 0, 0, 122);
    rcp_bytes_t             clear = rcp_cancel_encode_clear_all(1, 123);
    rcp_server_tick_ctx_t   ctx = base_ctx(0);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &a));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &b));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_tscf(srv, &clear, true, 5000000u, 0u));
    TEST_ASSERT_EQUAL_size_t(3, rcp_mock_server_pending_count(srv, 1));

    /* The reconstructed presentation instant, gPTP locked: the
     * cancellation is now due and, being ranked above TRIGGERED, is the
     * one rcp_server_endpoint_select_due() picks -- clearing itself and
     * both of its targets in one tick. */
    ctx.gptp_locked = true;
    ctx.gptp_now    = 5000000u;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_EQUAL_size_t(0, log.count); /* neither target's handler ever ran -- both were cancelled */

    rcp_bytes_free(&a);
    rcp_bytes_free(&b);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* REQ-SRV-013's own literal return-value claims: the three tests above
 * already prove cancel_all()/cancel_single()/cancel_non_safestate()'s
 * observable BEHAVIOR end-to-end through the mock server, but none of
 * them ever look at what the raw functions themselves return -- the
 * mock server's own apply_cancellation() (src/mock.c) voids every one
 * of these return values, since it reports outcome via a different
 * mechanism (RCP_MOCK_DISPATCH_CANCELLED plus, for clear-single, an
 * error-response payload). Calls rcp_server_endpoint_admit()/
 * cancel_all()/cancel_single() directly on a bare rcp_server_endpoint_t,
 * the same bypass-the-mock-server pattern
 * test_admit_takes_no_lifecycle_state_or_stream_identity() in
 * test_tc18_gaps_server.c already established -- admission itself never
 * looks at any sequencer table (that only matters later, at tick/
 * select_due time), so a compound request with any start_state at all
 * admits PENDING unconditionally. */
static void test_cancel_all_and_cancel_single_return_values(void)
{
    rcp_server_endpoint_t ep;
    rcp_compound_step_t   step = {0};
    rcp_bytes_t           a, b, c;
    uint8_t               request_type = 0xFFu;
    rcp_cancel_result_t   result;

    rcp_server_endpoint_init(&ep, true);

    step.sequencer_index = 1;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    a = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 3, &step, 0u, 21, NULL, 0);
    b = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 3, &step, 0u, 22, NULL, 0);
    c = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 3, &step, 0u, 23, NULL, 0);
    TEST_ASSERT_NOT_NULL(a.data);
    TEST_ASSERT_NOT_NULL(b.data);
    TEST_ASSERT_NOT_NULL(c.data);

    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                       rcp_server_endpoint_admit(&ep, a.data, a.len, 0u, false, 0u, 0u, &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                       rcp_server_endpoint_admit(&ep, b.data, b.len, 0u, false, 0u, 0u, &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                       rcp_server_endpoint_admit(&ep, c.data, c.len, 0u, false, 0u, 0u, &request_type, NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(3, rcp_server_endpoint_pending_count(&ep));

    /* cancel_single(): reports rcp_cancel_attempt()'s own outcome and
     * removes only the named entry -- the other two stay pending. */
    result = rcp_server_endpoint_cancel_single(&ep, 21, RCP_CANCEL_LIFECYCLE_QUEUED);
    TEST_ASSERT_EQUAL(RCP_CANCEL_RESULT_CANCELED, result);
    TEST_ASSERT_EQUAL_size_t(2, rcp_server_endpoint_pending_count(&ep));

    /* A transaction_num no longer present (already removed above): not
     * found, nothing further removed. */
    result = rcp_server_endpoint_cancel_single(&ep, 21, RCP_CANCEL_LIFECYCLE_QUEUED);
    TEST_ASSERT_EQUAL(RCP_CANCEL_RESULT_NOT_FOUND, result);
    TEST_ASSERT_EQUAL_size_t(2, rcp_server_endpoint_pending_count(&ep));

    /* cancel_all(): returns exactly how many it removed (2, the two
     * survivors) -- not just whether it removed anything, and not the
     * pre-cancel_single() count of 3. */
    TEST_ASSERT_EQUAL_size_t(2, rcp_server_endpoint_cancel_all(&ep));
    TEST_ASSERT_EQUAL_size_t(0, rcp_server_endpoint_pending_count(&ep));
    TEST_ASSERT_EQUAL_size_t(0, rcp_server_endpoint_cancel_all(&ep)); /* nothing left */

    rcp_bytes_free(&a);
    rcp_bytes_free(&b);
    rcp_bytes_free(&c);
    rcp_server_endpoint_destroy(&ep);
}

/* A watchdog overflow purges everything except the safety sequence, which
 * survives to drive the endpoint into its safe state. */
static void test_watchdog_purge_keeps_only_the_safety_sequence(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t plain  = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 111);
    rcp_bytes_t trig   = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 0, 0, 0, 0, 112);
    rcp_bytes_t safety = make_compound(RCP_REQUEST_TYPE_COMPOUND_SAFETY, 1,
                                        RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 113);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &plain));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &trig));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &safety));

    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_watchdog_purge(srv, 1));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    ctx.in_safe_state = true;
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND_SAFETY, log.opcode[0]);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&trig);
    rcp_bytes_free(&safety);
    rcp_mock_server_destroy(srv);
}

/* ── Cross-endpoint safe-state broadcast (issue #335, REQ-E2E-030) ─────────
 *
 * TC18 §12.7.7 Table 24's own rx_ovrflw_safestate_enable (relative address
 * 0x000D bit 3 in the real RC5 numbering -- corrected 2026-08-14, issue
 * #458; see regmap.h's own file-header note) brings every endpoint bound
 * to the request stream into its configured safe state when any one
 * endpoint's own request storage overflows -- not just the one endpoint
 * whose queue happened to fill up.
 * This proves that stream-wide half end-to-end: overflowing byte_bus_id
 * 1's own request store purges a non-safety-tagged request queued on
 * byte_bus_id 2, a sibling bound to the same request stream via
 * EP_ID_config (rcp_mock_server_set_ep_id_map(), issue #335) -- 2's own
 * queue never overflowed. */
static void test_overflow_on_one_endpoint_broadcasts_safe_state_to_stream_siblings(void)
{
    handler_log_t                    log;
    rcp_mock_server_t               *srv = fixture(&log);
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 1, 1}, /* ep 1, bbid 1, stream 1 -- the one that overflows */
        {2, 2, 1}, /* ep 2, bbid 2, stream 1 -- the sibling */
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];
    rcp_bytes_t                      frame;
    size_t                           i;

    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 2, 1, true, logging_handler, &log));
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

    /* fixture() already configured stream_cfg[0] (rx_stream_id=1) for
     * REQ-SEQ-013's own sequencer-ownership fixture -- refresh it here
     * with rx_ovrflw_safestate_enable also set, keeping rx_stream_id
     * unchanged so the resolve_index() lookup this batch's overflow check
     * (dispatch_plain(), mock.c) performs still finds it. */
    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id                = 1u;
    stream_cfg[0].rx_ovrflw_safestate_enable  = true;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    /* Give bbid 2 one non-safety-tagged (RCP_REQUEST_TYPE_TIMED, MSB
     * clear) stored request to later confirm gets purged. */
    frame = rcp_timed_encode_request(2, 0x1000u, 9u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_to(srv, 2, &frame));
    rcp_bytes_free(&frame);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 2));

    /* Fill bbid 1's own request storage to capacity. */
    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        frame = rcp_timed_encode_request(1, 0x1000u + (uint64_t)i, (uint8_t)i, NULL, 0u);
        TEST_ASSERT_NOT_NULL(frame.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_to(srv, 1, &frame));
        rcp_bytes_free(&frame);
    }
    TEST_ASSERT_EQUAL_size_t(RCP_SERVER_MAX_PENDING, rcp_mock_server_pending_count(srv, 1));

    /* One more request to bbid 1 overflows -- rejected locally (unchanged
     * behavior), AND broadcasts safe-state to every endpoint bound to
     * stream 1, including bbid 2. */
    frame = rcp_timed_encode_request(1, 0x9000u, 99u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit_to(srv, 1, &frame));
    rcp_bytes_free(&frame);

    /* The actual proof: bbid 2's own stored request was purged by the
     * broadcast, though bbid 2 itself never overflowed. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 2));

    rcp_mock_server_destroy(srv);
}

/* REQ-E2E-046 (issue #336): the identical overflow this file's own
 * broadcast test above already drives also latches stream_status[]'s
 * own overflow cause -- the readable Table 24 rx_stream_status bit,
 * a separate concern from the broadcast-safe-state actuator (this
 * server double's own reaction, not itself part of the register). */
static void test_overflow_latches_stream_status(void)
{
    handler_log_t                    log;
    rcp_mock_server_t               *srv = fixture(&log);
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];
    rcp_bytes_t                      frame;
    size_t                           i;

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id               = 1u;
    stream_cfg[0].rx_ovrflw_safestate_enable = true; /* gates
                                                          rcp_e2e_overflow_should_enter_safe_state(),
                                                          the same flag stream_status[]'s own
                                                          overflow latch is gated on too */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, 1u));

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        frame = rcp_timed_encode_request(1, 0x1000u + (uint64_t)i, (uint8_t)i, NULL, 0u);
        TEST_ASSERT_NOT_NULL(frame.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_to(srv, 1, &frame));
        rcp_bytes_free(&frame);
    }
    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, 1u)); /* still not overflowed */

    frame = rcp_timed_encode_request(1, 0x9000u, 99u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit_to(srv, 1, &frame));
    rcp_bytes_free(&frame);

    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, 1u));

    rcp_mock_server_destroy(srv);
}

/* Negative control: identical setup, EXCEPT srv's own EP_ID_config table
 * is left empty -- confirms the broadcast above is a genuine consequence
 * of EP_ID_config's own content, not something dispatch_plain() would
 * have done anyway. Without a bound-endpoint table, bbid 2's own pending
 * request survives the overflow on bbid 1. */
static void test_overflow_does_not_broadcast_without_an_ep_id_map(void)
{
    handler_log_t                    log;
    rcp_mock_server_t               *srv = fixture(&log);
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];
    rcp_bytes_t                      frame;
    size_t                           i;

    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 2, 1, true, logging_handler, &log));
    /* Deliberately no rcp_mock_server_set_ep_id_map() call. */

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id               = 1u;
    stream_cfg[0].rx_ovrflw_safestate_enable = true;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    frame = rcp_timed_encode_request(2, 0x1000u, 9u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_to(srv, 2, &frame));
    rcp_bytes_free(&frame);

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        frame = rcp_timed_encode_request(1, 0x1000u + (uint64_t)i, (uint8_t)i, NULL, 0u);
        TEST_ASSERT_NOT_NULL(frame.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit_to(srv, 1, &frame));
        rcp_bytes_free(&frame);
    }

    frame = rcp_timed_encode_request(1, 0x9000u, 99u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit_to(srv, 1, &frame));
    rcp_bytes_free(&frame);

    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 2));

    rcp_mock_server_destroy(srv);
}

/* ── Chained requests, across one frame's members ─────────────────────────── */

/* Room for the small two-member frames these tests build. */
#define FRAME_BUF_CAP ((size_t)256u)

/* Concatenates two already-encoded ACF messages into out[0..cap), as a
 * single NTSCF/TSCF frame carries them back to back, and returns the
 * combined length. Deliberately writes into caller-owned storage rather
 * than allocating: these frames are a few dozen octets. */
static size_t concat2(uint8_t *out, size_t cap, const rcp_bytes_t *a, const rcp_bytes_t *b)
{
    TEST_ASSERT_TRUE(a->len + b->len <= cap);

    rcp_memcpy_bounded(out, cap, a->data, a->len);
    rcp_memcpy_bounded(out + a->len, cap - a->len, b->data, b->len);
    return a->len + b->len;
}

/* concat2()'s own three- and four-member counterparts, same contract. */
static size_t concat3(uint8_t *out, size_t cap, const rcp_bytes_t *a, const rcp_bytes_t *b,
                       const rcp_bytes_t *c)
{
    TEST_ASSERT_TRUE(a->len + b->len + c->len <= cap);

    rcp_memcpy_bounded(out, cap, a->data, a->len);
    rcp_memcpy_bounded(out + a->len, cap - a->len, b->data, b->len);
    rcp_memcpy_bounded(out + a->len + b->len, cap - a->len - b->len, c->data, c->len);
    return a->len + b->len + c->len;
}

static size_t concat4(uint8_t *out, size_t cap, const rcp_bytes_t *a, const rcp_bytes_t *b,
                       const rcp_bytes_t *c, const rcp_bytes_t *d)
{
    TEST_ASSERT_TRUE(a->len + b->len + c->len + d->len <= cap);

    rcp_memcpy_bounded(out, cap, a->data, a->len);
    rcp_memcpy_bounded(out + a->len, cap - a->len, b->data, b->len);
    rcp_memcpy_bounded(out + a->len + b->len, cap - a->len - b->len, c->data, c->len);
    rcp_memcpy_bounded(out + a->len + b->len + c->len, cap - a->len - b->len - c->len, d->data, d->len);
    return a->len + b->len + c->len + d->len;
}

/* §11.2.2.4: "If the first request in an AVTPDU is a chain request, then
 * there is no predecessor to chain to, thus the entire chain will be
 * ignored." */
static void test_chained_first_in_frame_is_chain_error(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t first  = rcp_chained_encode_member(1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 121,
                                                    NULL, 0);
    rcp_bytes_t second = rcp_chained_encode_member(1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 122,
                                                    NULL, 0);
    uint8_t frame[FRAME_BUF_CAP];
    size_t frame_len = concat2(frame, sizeof(frame), &first, &second);
    rcp_mock_frame_member_result_t results[4];
    size_t n;

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CHAIN_ERROR, results[0].result);
    /* And the rest of the chain is ignored too. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CHAIN_ABORTED, results[1].result);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&first);
    rcp_bytes_free(&second);
    rcp_mock_server_destroy(srv);
}

/* Extends the test above to check the actual response bytes: each member
 * of a broken chain gets its own error response (TC18 §11.2.2.4: "An
 * error response with the error code 'CHAIN_ERROR' to each request will
 * be generated by the respective EPs"), carrying that member's own
 * transaction_num, not a shared/aggregate one. */
static void test_chained_first_in_frame_sends_per_member_error_responses(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_bytes_t                  first  = rcp_chained_encode_member(
        1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 121, NULL, 0);
    rcp_bytes_t                  second = rcp_chained_encode_member(
        1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 122, NULL, 0);
    uint8_t                      frame[FRAME_BUF_CAP];
    size_t                       frame_len = concat2(frame, sizeof(frame), &first, &second);
    rcp_mock_frame_member_result_t results[4];
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;
    size_t                        n;

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);

    TEST_ASSERT_NOT_NULL(results[0].response.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(results[0].response.data,
                                                      results[0].response.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(121u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_CHAIN_ERROR, payload[0]);

    TEST_ASSERT_NOT_NULL(results[1].response.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(results[1].response.data,
                                                      results[1].response.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(122u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_CHAIN_ABORTED, payload[0]);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&first);
    rcp_bytes_free(&second);
    rcp_mock_server_destroy(srv);
}

/* A chained member following a real predecessor is stored, and executes
 * once its chain_exec_delay elapses. */
static void test_chained_member_after_predecessor_executes(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_acf_byte_message_info_t info = {0};
    rcp_bytes_t lead;
    rcp_bytes_t member;
    uint8_t frame[FRAME_BUF_CAP];
    size_t frame_len;
    rcp_mock_frame_member_result_t results[4];
    rcp_server_tick_ctx_t ctx;
    size_t n;

    info.byte_bus_id     = 1;
    info.transaction_num = 131;
    lead = rcp_acf_encode_abb(&info, NULL, 0);
    TEST_ASSERT_NOT_NULL(lead.data);

    member = rcp_chained_encode_member(1, 10 /* chain_exec_delay */,
                                        RCP_CHAINED_CS_CONTINUE_ON_ERROR, 132, NULL, 0);
    TEST_ASSERT_NOT_NULL(member.data);
    frame_len = concat2(frame, sizeof(frame), &lead, &member);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    /* The lead request is standard: it ran immediately. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    /* The chained member is stored, awaiting its delay. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[1].result);
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    ctx = base_ctx(9);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    ctx = base_ctx(10);
    TEST_ASSERT_TRUE(tick(srv, &ctx));

    TEST_ASSERT_EQUAL_size_t(2, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_CHAINED, log.opcode[1]);

    rcp_bytes_free(&lead);
    rcp_bytes_free(&member);
    rcp_mock_server_destroy(srv);
}

/* ── ep_enable gates conditional-request execution (REQ-SRV-015/016
 *    extension, issue #461) ──────────────────────────────────────────────── */

/* rcp_server_endpoint_admit()'s conditional-request path (server.c) never
 * consulted ep->ep_enable at all -- a Compound/Compound Wait/Triggered/
 * Timed/Chained request was ALWAYS stored regardless of ep_enable (that
 * half was, and remains, correct: TC18 §12.3.1.3 says an operational
 * request is stored while an EP is disabled), but nothing downstream ever
 * re-checked ep_enable before letting the stored request run once its own
 * condition became due -- so a disabled endpoint's Compound/Triggered/
 * Timed/Chained request still EXECUTED, violating the "will only execute
 * config requests" half of the same TC18 rule. These four tests each
 * mirror an existing enabled-endpoint test above (test_compound_never_
 * fires_while_endpoint_busy(), test_triggered_executes_only_on_its_own_
 * trigger(), test_timed_waits_for_its_presentation_time(),
 * test_chained_member_after_predecessor_executes()) with one addition:
 * the endpoint starts disabled, its own kind-specific condition is driven
 * fully due, and the handler must NOT run -- then, only once the endpoint
 * is explicitly re-enabled (rcp_mock_server_set_endpoint_enable(), the
 * same primitive rcp_mock_server_pwrmode_resume()'s own re-enable loop in
 * src/mock.c uses for the analogous Standard-request queued->drain
 * transition), the identical still-pending request finally executes on
 * the very next tick -- proving the fix gates EXECUTION, not ADMISSION:
 * the request was genuinely stored and waiting the whole time, not
 * silently dropped. */

//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
static void test_disabled_endpoint_queues_compound_request_without_executing_it(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    /* start_state 1 == the power-on state, so the start condition holds
     * from admission and the (zero) delay is already elapsed -- once
     * enabled, only ep_enable itself stands between this and executing. */
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 4, 0, 0, 51);
    rcp_server_tick_ctx_t ctx = base_ctx(0);
    uint8_t got = 0;

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, false));

    TEST_ASSERT_NOT_NULL(frame.data);
    /* Still stored even while disabled -- admission itself is unaffected
     * by this fix, exactly as REQ-SRV-015's own text for admit()'s
     * conditional path establishes. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    /* Condition fully due, endpoint idle -- an enabled endpoint would fire
     * on this very tick (see test_compound_never_fires_while_endpoint_
     * busy()). Disabled, it must not, however many times ticked. */
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1)); /* still stored */

    /* Re-enabled: the SAME still-pending request now runs, on the very
     * next tick, with no re-submission. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, true));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_COMPOUND, log.opcode[0]);
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(rcp_mock_server_sequencers(srv), 0, &got));
    TEST_ASSERT_EQUAL_UINT8(4, got);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
static void test_disabled_endpoint_queues_triggered_request_without_executing_it(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_triggered(RCP_REQUEST_TYPE_TRIGGERED, 6, 2, 0, 0, 52);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, false));

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* Trigger occurrences are still counted while disabled -- notify_
     * trigger() has no ep_enable concept of its own, and shouldn't need
     * one: only EXECUTION is gated, not occurrence bookkeeping. */
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_notify_trigger(srv, 6, 2));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, true));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TRIGGERED, log.opcode[0]);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
static void test_disabled_endpoint_queues_timed_request_without_executing_it(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    const uint64_t pt = 0x0000A5A500000064ull;
    rcp_bytes_t frame = rcp_timed_encode_request(1, pt, 53, NULL, 0);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, false));

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    /* presentation_time already reached -- an enabled endpoint would fire
     * (see test_timed_waits_for_its_presentation_time()). */
    ctx.gptp_now = pt;
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, true));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_TIMED, log.opcode[0]);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
static void test_disabled_endpoint_queues_chained_request_without_executing_it(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_acf_byte_message_info_t info = {0};
    rcp_bytes_t lead;
    rcp_bytes_t member;
    uint8_t frame[FRAME_BUF_CAP];
    size_t frame_len;
    rcp_mock_frame_member_result_t results[4];
    rcp_server_tick_ctx_t ctx;
    size_t n;

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, false));

    info.byte_bus_id     = 1;
    info.transaction_num = 141;
    lead = rcp_acf_encode_abb(&info, NULL, 0);
    TEST_ASSERT_NOT_NULL(lead.data);

    member = rcp_chained_encode_member(1, 10 /* chain_exec_delay */,
                                        RCP_CHAINED_CS_CONTINUE_ON_ERROR, 142, NULL, 0);
    TEST_ASSERT_NOT_NULL(member.data);
    frame_len = concat2(frame, sizeof(frame), &lead, &member);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    /* The lead is a plain standard request: while disabled it queues
     * (server.h's own pre-existing ep_enable queue) rather than running --
     * this does not count as a chain error (server.c's own dispatch_frame()
     * only treats unknown-bus/rejected/dropped as breaking the chain), so
     * the chained member behind it is still admitted normally. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_QUEUED, results[0].result);
    /* The chained member is stored, its predecessor already marked done
     * (chain_exec_delay starts running immediately, same as the enabled-
     * endpoint test) -- but it must not execute while disabled. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[1].result);
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    /* chain_exec_delay (10) fully elapsed -- an enabled endpoint would fire
     * (see test_chained_member_after_predecessor_executes()). */
    ctx = base_ctx(10);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_enable(srv, 1, true));
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);
    TEST_ASSERT_EQUAL_HEX8(RCP_REQUEST_TYPE_CHAINED, log.opcode[0]);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&lead);
    rcp_bytes_free(&member);
    rcp_mock_server_destroy(srv);
}

/* ── Chain cascade cancellation (REQ-CANCEL-012, issue #334) ──────────────── */

/* TC18 §11.2.3: "If a request is cancelled to which a request is chained,
 * then the chained successors shall be cancelled by the RC Server as
 * well." Cancelling the FIRST chained member of a two-member chain must
 * also remove the second, even though the clear-single request only ever
 * names the first member's own transaction_num. */
static void test_clear_single_cascade_removes_chained_successors(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_acf_byte_message_info_t info = {0};
    rcp_bytes_t lead;
    rcp_bytes_t member_a;
    rcp_bytes_t member_b;
    rcp_bytes_t clear;
    uint8_t frame[FRAME_BUF_CAP];
    size_t frame_len;
    rcp_mock_frame_member_result_t results[4];
    rcp_server_tick_ctx_t ctx;
    size_t n;

    info.byte_bus_id     = 1;
    info.transaction_num = 200;
    lead = rcp_acf_encode_abb(&info, NULL, 0); /* standard, executes immediately */
    TEST_ASSERT_NOT_NULL(lead.data);

    member_a = rcp_chained_encode_member(1, 10 /* chain_exec_delay */,
                                          RCP_CHAINED_CS_CONTINUE_ON_ERROR, 201, NULL, 0);
    member_b = rcp_chained_encode_member(1, 10, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 202, NULL, 0);
    TEST_ASSERT_NOT_NULL(member_a.data);
    TEST_ASSERT_NOT_NULL(member_b.data);

    frame_len = concat3(frame, sizeof(frame), &lead, &member_a, &member_b);
    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);      /* lead ran immediately */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[1].result); /* member_a stored */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[2].result); /* member_b stored */
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    /* Cancel only the FIRST chained member (201) -- its own successor
     * (202) must cascade with it, even though nothing named 202
     * directly. */
    clear = rcp_cancel_encode_clear_single(1, 201, 203);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED, submit(srv, &clear));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

    /* Neither chained member ever fires -- only the lead request ran. */
    ctx = base_ctx(50);
    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(1, log.count);

    rcp_bytes_free(&lead);
    rcp_bytes_free(&member_a);
    rcp_bytes_free(&member_b);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* Two independent chains, back to back in one frame, must not cascade
 * into one another: cancelling the first chain's own member leaves the
 * second, unrelated chain's member untouched. Guards against a cascade
 * implementation that matches by frame position alone, without also
 * checking chain identity. */
static void test_clear_single_cascade_does_not_cross_chains(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_acf_byte_message_info_t lead1_info = {0};
    rcp_acf_byte_message_info_t lead2_info = {0};
    rcp_bytes_t lead1;
    rcp_bytes_t member1;
    rcp_bytes_t lead2;
    rcp_bytes_t member2;
    rcp_bytes_t clear;
    uint8_t frame[FRAME_BUF_CAP];
    size_t frame_len;
    rcp_mock_frame_member_result_t results[4];
    rcp_server_tick_ctx_t ctx;
    size_t n;

    lead1_info.byte_bus_id     = 1;
    lead1_info.transaction_num = 210;
    lead1   = rcp_acf_encode_abb(&lead1_info, NULL, 0);
    member1 = rcp_chained_encode_member(1, 10, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 211, NULL, 0);

    lead2_info.byte_bus_id     = 1;
    lead2_info.transaction_num = 220;
    lead2   = rcp_acf_encode_abb(&lead2_info, NULL, 0);
    member2 = rcp_chained_encode_member(1, 10, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 221, NULL, 0);

    TEST_ASSERT_NOT_NULL(lead1.data);
    TEST_ASSERT_NOT_NULL(member1.data);
    TEST_ASSERT_NOT_NULL(lead2.data);
    TEST_ASSERT_NOT_NULL(member2.data);

    frame_len = concat4(frame, sizeof(frame), &lead1, &member1, &lead2, &member2);
    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[1].result);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[3].result);
    TEST_ASSERT_EQUAL_size_t(2, rcp_mock_server_pending_count(srv, 1));

    /* Cancel the FIRST chain's own member -- the second, unrelated
     * chain's member must survive. */
    clear = rcp_cancel_encode_clear_single(1, 211, 230);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CANCELLED, submit(srv, &clear));
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    ctx = base_ctx(50);
    TEST_ASSERT_TRUE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_UINT8(221, log.transaction_num[log.count - 1]);

    rcp_bytes_free(&lead1);
    rcp_bytes_free(&member1);
    rcp_bytes_free(&lead2);
    rcp_bytes_free(&member2);
    rcp_bytes_free(&clear);
    rcp_mock_server_destroy(srv);
}

/* ── Malformed conditional requests are rejected, not executed ────────────── */

static void test_undecodable_conditional_request_is_rejected(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = rcp_timed_encode_request(1, 5, 141, NULL, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    /* Set the mandatorily-zero reserved octet: the message still claims
     * request_type 0x0A, but no longer decodes as a timed request. */
    frame.data[RCP_ACF_ABB_HEADER_LEN + 1] = 0xFFu;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, submit(srv, &frame));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));
    TEST_ASSERT_EQUAL_size_t(0, log.count);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* A server with no sequencer table at all cannot evaluate a compound
 * request's condition, so it never executes one -- fail closed. */
static void test_compound_never_due_without_a_sequencer_table(void)
{
    handler_log_t log;
    rcp_mock_server_t *srv = fixture(&log);
    rcp_bytes_t frame = make_compound(RCP_REQUEST_TYPE_COMPOUND, 0,
                                       RCP_SEQUENCER_POWER_ON_STATE, 0, 0, 0, 151);
    rcp_server_tick_ctx_t ctx = base_ctx(0);

    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 0)); /* unsupported */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, submit(srv, &frame));

    TEST_ASSERT_FALSE(tick(srv, &ctx));
    TEST_ASSERT_EQUAL_size_t(0, log.count);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 1));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_standard_request_still_executes_immediately);

    RUN_TEST(test_compound_waits_for_its_sequencer_state);
    RUN_TEST(test_compound_never_fires_while_endpoint_busy);
    RUN_TEST(test_compound_exec_delay_holds_execution_back);
    RUN_TEST(test_compound_wait_requires_the_wait_condition);
    RUN_TEST(test_two_pending_compound_waits_have_independent_targets);
    RUN_TEST(test_compound_wait_reserved_evt_is_rejected_at_admission);
    RUN_TEST(test_compound_wait_reserved_evt_sends_err_response);
    RUN_TEST(test_admission_rejection_acknowledge_sent_when_evt3_requests_it);
    RUN_TEST(test_admission_rejection_acknowledge_suppressed_when_evt3_not_requested);
    RUN_TEST(test_pending_conditional_request_emits_requested_acknowledge);
    RUN_TEST(test_pending_conditional_request_no_acknowledge_when_evt3_clear);

    RUN_TEST(test_compound_admission_denied_for_unclaimed_sequencer);
    RUN_TEST(test_compound_admission_denied_for_sequencer_owned_by_a_different_client);
    RUN_TEST(test_compound_admission_rejected_for_unknown_sequencer_index_reports_sequencer_not_known);
    RUN_TEST(test_compound_wait_admission_denied_for_unclaimed_sequencer);
    RUN_TEST(test_compound_admission_permitted_when_no_sequencer_table_configured);

    RUN_TEST(test_triggered_executes_only_on_its_own_trigger);
    RUN_TEST(test_triggered_threshold_delays_execution);
    RUN_TEST(test_triggered_never_fires_while_endpoint_busy);
    RUN_TEST(test_gptp_lock_established_arms_a_waiting_triggered_request);
    RUN_TEST(test_gptp_lock_signals_are_not_conflated);
    RUN_TEST(test_gptp_lock_state_respects_source_ep);

    RUN_TEST(test_timed_waits_for_its_presentation_time);
    RUN_TEST(test_timed_never_due_without_gptp_lock);
    RUN_TEST(test_timed_request_rejected_at_admission_when_time_sync_not_supported);
    RUN_TEST(test_compound_admission_unaffected_by_time_sync_not_supported);

    RUN_TEST(test_safety_tagged_request_waits_for_safe_state);

    RUN_TEST(test_due_requests_execute_in_priority_order);
    RUN_TEST(test_equal_rank_requests_execute_in_arrival_order);

    RUN_TEST(test_repeat_count_controls_how_often_a_request_runs);
    RUN_TEST(test_infinite_repeat_count_is_never_exhausted);

    RUN_TEST(test_clear_all_empties_the_request_store);
    RUN_TEST(test_clear_single_removes_only_its_target);
    RUN_TEST(test_clear_single_not_found_sends_request_not_found_error);
    RUN_TEST(test_clear_non_safestate_keeps_safety_tagged_requests);
    RUN_TEST(test_tscf_cancellation_with_future_presentation_time_is_deferred);
    RUN_TEST(test_tscf_cancellation_executes_once_presentation_time_passes);
    RUN_TEST(test_cancel_all_and_cancel_single_return_values);
    RUN_TEST(test_watchdog_purge_keeps_only_the_safety_sequence);
    RUN_TEST(test_overflow_on_one_endpoint_broadcasts_safe_state_to_stream_siblings);
    RUN_TEST(test_overflow_latches_stream_status);
    RUN_TEST(test_overflow_does_not_broadcast_without_an_ep_id_map);

    RUN_TEST(test_chained_first_in_frame_is_chain_error);
    RUN_TEST(test_chained_first_in_frame_sends_per_member_error_responses);
    RUN_TEST(test_chained_member_after_predecessor_executes);

    RUN_TEST(test_disabled_endpoint_queues_compound_request_without_executing_it);
    RUN_TEST(test_disabled_endpoint_queues_triggered_request_without_executing_it);
    RUN_TEST(test_disabled_endpoint_queues_timed_request_without_executing_it);
    RUN_TEST(test_disabled_endpoint_queues_chained_request_without_executing_it);

    RUN_TEST(test_clear_single_cascade_removes_chained_successors);
    RUN_TEST(test_clear_single_cascade_does_not_cross_chains);

    RUN_TEST(test_undecodable_conditional_request_is_rejected);
    RUN_TEST(test_compound_never_due_without_a_sequencer_table);

    return UNITY_END();
}
