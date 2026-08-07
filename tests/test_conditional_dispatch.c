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
//cfusa:test REQ-ACF-031
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

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/request_cancel.h>
#include <rcp/request_chained.h>
#include <rcp/request_compound.h>
#include <rcp/request_sequencer.h>
#include <rcp/request_timed.h>
#include <rcp/request_triggered.h>
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
 * sequencer table, and the logging handler attached. */
static rcp_mock_server_t *fixture(handler_log_t *log)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    memset(log, 0, sizeof(*log));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint(srv, 1, 1, true /* ep_enable */, logging_handler, log));
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 4));
    return srv;
}

static rcp_mock_dispatch_result_t submit(rcp_mock_server_t *srv, const rcp_bytes_t *frame)
{
    rcp_bytes_t                resp = {0};
    rcp_mock_dispatch_result_t r;

    r = rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, true,
                                  frame->data, frame->len, &resp);
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

/* TC18 §12.9.6: "The error response shall contain the byte_bus_id and
 * transaction number of the request. The error response shall contain a
 * byte_msg_payload with an error code." Extends the rejection test above
 * to check the actual wire bytes, not just the dispatch-result enum --
 * this is the one rejection path github.com/SoundMatt/c-RCP/issues/163
 * currently wires end to end. */
static void test_compound_wait_reserved_evt_sends_unsupported_cmd_error_response(void)
{
    handler_log_t                log;
    rcp_mock_server_t           *srv = fixture(&log);
    rcp_compound_step_t          step = {0};
    rcp_bytes_t                  frame, resp = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    step.start_state = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state    = 1;
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 1, &step, 0x3u, 30, NULL,
                                         0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, frame.data, frame.len,
                                                &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(30u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_UNSUPPORTED_CMD, payload[0]);

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

    memcpy(out, a->data, a->len);
    memcpy(out + a->len, b->data, b->len);
    return a->len + b->len;
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

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, frame, frame_len,
                                        results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CHAIN_ERROR, results[0].result);
    /* And the rest of the chain is ignored too. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CHAIN_ABORTED, results[1].result);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 1));

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

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, frame, frame_len,
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
    RUN_TEST(test_compound_exec_delay_holds_execution_back);
    RUN_TEST(test_compound_wait_requires_the_wait_condition);
    RUN_TEST(test_two_pending_compound_waits_have_independent_targets);
    RUN_TEST(test_compound_wait_reserved_evt_is_rejected_at_admission);
    RUN_TEST(test_compound_wait_reserved_evt_sends_unsupported_cmd_error_response);

    RUN_TEST(test_triggered_executes_only_on_its_own_trigger);
    RUN_TEST(test_triggered_threshold_delays_execution);
    RUN_TEST(test_triggered_never_fires_while_endpoint_busy);

    RUN_TEST(test_timed_waits_for_its_presentation_time);
    RUN_TEST(test_timed_never_due_without_gptp_lock);

    RUN_TEST(test_safety_tagged_request_waits_for_safe_state);

    RUN_TEST(test_due_requests_execute_in_priority_order);
    RUN_TEST(test_equal_rank_requests_execute_in_arrival_order);

    RUN_TEST(test_repeat_count_controls_how_often_a_request_runs);
    RUN_TEST(test_infinite_repeat_count_is_never_exhausted);

    RUN_TEST(test_clear_all_empties_the_request_store);
    RUN_TEST(test_clear_single_removes_only_its_target);
    RUN_TEST(test_clear_non_safestate_keeps_safety_tagged_requests);
    RUN_TEST(test_watchdog_purge_keeps_only_the_safety_sequence);

    RUN_TEST(test_chained_first_in_frame_is_chain_error);
    RUN_TEST(test_chained_member_after_predecessor_executes);

    RUN_TEST(test_undecodable_conditional_request_is_rejected);
    RUN_TEST(test_compound_never_due_without_a_sequencer_table);

    return UNITY_END();
}
