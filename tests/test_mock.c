/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-MOCK-001
//cfusa:test REQ-MOCK-002
//cfusa:test REQ-MOCK-003
//cfusa:test REQ-MOCK-004
//cfusa:test REQ-MOCK-005
//cfusa:test REQ-MOCK-006
//cfusa:test REQ-MOCK-007
//cfusa:test REQ-MOCK-008
//cfusa:test REQ-MOCK-009
//cfusa:test REQ-MOCK-010
//cfusa:test REQ-MOCK-011
//cfusa:test REQ-MOCK-012
//cfusa:test REQ-MOCK-013
//cfusa:test REQ-MOCK-014
//cfusa:test REQ-MOCK-015
//cfusa:test REQ-MOCK-016
//cfusa:test REQ-MOCK-017
//cfusa:test REQ-MOCK-018
//cfusa:test REQ-MOCK-019
//cfusa:test REQ-MOCK-020
//cfusa:test REQ-MOCK-030
//cfusa:test REQ-MOCK-031
//cfusa:test REQ-WDG-010
//cfusa:test REQ-AVTP-021
//cfusa:test REQ-AVTP-022
//cfusa:test REQ-AVTP-023
/* Tests the TC18-shaped RC-Server/endpoint test double (ROADMAP.md
 * milestone 77). The pre-TC18 zone-controller mock this file used to test
 * moved to tests/legacy_mock.h/.c; tests/test_legacy_mock.c (a renamed
 * copy of this file's own old content) now tests that instead. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/watchdog.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* An empty plausibility snapshot: vacuously satisfies both lifecycle.h
 * transition guards (no endpoint/request-stream claims to be inconsistent
 * about), enough to drive a fresh rcp_mock_server_t from
 * HW_UNCONFIGURED to HW_CONFIGURED in these tests without needing a full
 * regmap-backed fixture. */
static const rcp_lifecycle_plausibility_snapshot_t EMPTY_SNAP = {NULL, 0, NULL, 0};

/* HW_UNCONFIGURED -> HW_CONFIGURED does not consult writer (see
 * lifecycle.h's own rcp_lifecycle_transition() doc comment), so a plain
 * {0} is sufficient and correct here, not just a convenience default.
 * As of the REQ-LIFECYCLE-022 fix, this advance also requires
 * all_other_eps_idle -- true here, since this fixture is not itself
 * testing idleness. */
static void to_hw_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP, none, true));
}

/* As of the REQ-LIFECYCLE-032 fix, HW_CONFIGURED admits only requests to
 * EP0 (byte_bus_id 0) -- a fixture dispatching to any other endpoint needs
 * RCP_CONFIGURED instead. EMPTY_SNAP's zero endpoint/request-stream counts
 * trivially satisfy both plausibility checks along the way, same as
 * to_hw_configured() already relies on for its own single transition. As
 * of the REQ-LIFECYCLE-031 fix, the HW_CONFIGURED -> RCP_CONFIGURED
 * advance also requires an authorized writer (not idle-gated, per
 * Figure 16) -- root is used here since this fixture is not itself
 * testing lifecycle authorization policy. */
static void to_rcp_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    to_hw_configured(srv);
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));
}

/* ── Server lifecycle ──────────────────────────────────────────────────────── */

static void test_new_server_starts_hw_unconfigured(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_mock_server_state(srv));
    rcp_mock_server_destroy(srv);
}

static void test_transition_passthrough_valid(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    to_hw_configured(srv);
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, rcp_mock_server_state(srv));
    rcp_mock_server_destroy(srv);
}

static void test_transition_passthrough_rejects_invalid(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_mock_server_state(srv));
    rcp_mock_server_destroy(srv);
}

static void test_destroy_null_is_safe(void)
{
    rcp_mock_server_destroy(NULL);
    TEST_PASS();
}

/* ── Register map access ───────────────────────────────────────────────────── */

static void test_new_server_regmap_starts_empty(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    const rcp_regmap_general_t *map = rcp_mock_server_regmap(srv);

    TEST_ASSERT_EQUAL_UINT16(0, map->svr_ep_count);
    TEST_ASSERT_EQUAL_UINT16(RCP_REGMAP_NO_ROOT_CLIENT, map->svr_root_client_index);

    rcp_mock_server_destroy(srv);
}

static void test_regmap_is_mutable(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_regmap_general_t *map = rcp_mock_server_regmap(srv);

    map->vendor_id = 0x1234;
    TEST_ASSERT_EQUAL_UINT16(0x1234, rcp_mock_server_regmap(srv)->vendor_id);

    rcp_mock_server_destroy(srv);
}

/* ── Endpoint registration ─────────────────────────────────────────────────── */

static void test_add_endpoint_increments_svr_ep_count(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 2, 6, false, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(2, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_add_endpoint_duplicate_bus_id_rejected(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_ERR_DUPLICATE_BUS_ID,
        rcp_mock_server_add_endpoint(srv, 1, 9, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_add_endpoint_capacity_exhausted(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    size_t i;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        TEST_ASSERT_EQUAL(RCP_MOCK_OK,
            rcp_mock_server_add_endpoint(srv, (rcp_byte_bus_id_t)i, 1, true, NULL, NULL));
    }
    TEST_ASSERT_EQUAL(RCP_MOCK_ERR_CAPACITY,
        rcp_mock_server_add_endpoint(srv, (rcp_byte_bus_id_t)RCP_MOCK_MAX_ENDPOINTS, 1, true, NULL, NULL));

    rcp_mock_server_destroy(srv);
}

/* ── REQ-MOCK-031 (TC18 §12.9.1, issue #432): stream-scoped byte_bus_id
 * lookup ─────────────────────────────────────────────────────────────────
 *
 * "In dependence on the stream_id and byte_bus_id the RC Server
 * determines the endpoint that is addressed." Proves the SAME
 * byte_bus_id can validly address two DIFFERENT endpoints registered on
 * two DIFFERENT stream_ids -- something rcp_mock_server_add_endpoint()'s
 * own flat, server-wide namespace could never permit (see
 * test_add_endpoint_duplicate_bus_id_rejected() above, which stays
 * correct and unaffected: that is exactly the guarantee callers of the
 * plain, unscoped API still get). */

#define STREAM_A ((uint64_t)0x1111111111111111ULL)
#define STREAM_B ((uint64_t)0x2222222222222222ULL)
#define STREAM_C ((uint64_t)0x3333333333333333ULL)

static void test_add_endpoint_on_stream_allows_same_bus_id_on_different_streams(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    /* Same byte_bus_id (5), two different stream_ids -- both succeed. */
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_A, 5, 1, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_B, 5, 1, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(2, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_add_endpoint_on_stream_duplicate_on_same_stream_rejected(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_A, 5, 1, true, NULL, NULL));
    /* Same byte_bus_id AND same stream_id -- still rejected. */
    TEST_ASSERT_EQUAL(RCP_MOCK_ERR_DUPLICATE_BUS_ID,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_A, 5, 1, true, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_add_endpoint_on_stream_rejected_when_unscoped_endpoint_already_registered(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    /* An unscoped (plain add_endpoint()) slot at byte_bus_id 5 matches
     * every stream_id -- registering a stream-scoped slot at the same
     * byte_bus_id on ANY stream would make lookup ambiguous, so it is
     * rejected exactly like a same-stream duplicate. */
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 5, 1, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_ERR_DUPLICATE_BUS_ID,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_A, 5, 1, true, NULL, NULL));

    rcp_mock_server_destroy(srv);
}

static void test_remove_endpoint_decrements_svr_ep_count(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL);
    rcp_mock_server_add_endpoint(srv, 2, 6, true, NULL, NULL);

    TEST_ASSERT_TRUE(rcp_mock_server_remove_endpoint(srv, 1));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_remove_endpoint_unknown_bus_returns_false(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    TEST_ASSERT_FALSE(rcp_mock_server_remove_endpoint(srv, 42));
    rcp_mock_server_destroy(srv);
}

static void test_readd_after_remove_succeeds(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL);
    rcp_mock_server_remove_endpoint(srv, 1);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 7, true, NULL, NULL));

    rcp_mock_server_destroy(srv);
}

static void test_set_endpoint_enable_unknown_bus_returns_false(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    TEST_ASSERT_FALSE(rcp_mock_server_set_endpoint_enable(srv, 42, true));
    rcp_mock_server_destroy(srv);
}

static void test_queue_len_unknown_bus_is_zero(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    TEST_ASSERT_EQUAL_UINT(0, rcp_mock_server_endpoint_queue_len(srv, 42));
    rcp_mock_server_destroy(srv);
}

/* ── Dispatch ───────────────────────────────────────────────────────────────── */

static bool    g_handler_called;
static uint8_t g_seen_request[16];
static size_t  g_seen_request_len;
static void   *g_seen_user_data;

static void echo_handler(const uint8_t *request, size_t request_len, rcp_bytes_t *out_response,
                          void *user_data)
{
    g_handler_called    = true;
    g_seen_request_len  = request_len;
    g_seen_user_data    = user_data;
    if (request_len > 0 && request_len <= sizeof(g_seen_request)) {
        memcpy(g_seen_request, request, request_len);
    }
    *out_response = rcp_bytes_dup(request, request_len);
}

static void reset_handler_capture(void)
{
    g_handler_called   = false;
    g_seen_request_len = 0;
    g_seen_user_data   = NULL;
    memset(g_seen_request, 0, sizeof(g_seen_request));
}

static void test_dispatch_dropped_by_lifecycle(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new(); /* still HW_UNCONFIGURED */
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0xAA};

    /* A TSCF-headed frame is dropped outright while HW_UNCONFIGURED,
     * regardless of endpoint registration. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_DROPPED,
        rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* REQ-LIFECYCLE-033: a GBB-framed request correctly addressed to EP0
 * while HW_UNCONFIGURED is answered with RCP_ERROR_REQUEST_REJECTED
 * (TC18 §12.7), not silently dropped -- rcp_mock_server_dispatch()'s own
 * REJECT-handling branch, mirroring test_dispatch_unknown_bus_sends_
 * ep_not_found_error()'s response-decoding style. */
static void test_dispatch_rejected_by_lifecycle_sends_request_rejected_error(void)
{
    rcp_mock_server_t           *srv = rcp_mock_server_new(); /* still HW_UNCONFIGURED */
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame, resp = {0};
    rcp_acf_byte_message_info_t  resp_hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    hdr.byte_bus_id     = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.transaction_num = 77;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0); /* frame bytes: irrelevant to
                                                   should_accept()'s own
                                                   msg-type classification,
                                                   which is this call's own
                                                   acf_msg_type parameter
                                                   below, not decoded from
                                                   the frame itself */
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
        rcp_mock_server_dispatch(srv, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_SUBTYPE_NTSCF,
                                  RCP_ACF_MSG_TYPE_GBB, false, 1u, frame.data, frame.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &resp_hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&resp_hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, resp_hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(77u, resp_hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_REJECTED, payload[0]);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-WDG-010: rcp_mock_server_dispatch() kicks the per-stream watchdog ── */

static void wdg_busy_wait_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();

    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait: no sleep primitive is exported by rcp/clock.h, same
         * as every other timing-based test in this codebase (see
         * test_watchdog.c's own test_sleep_ms and
         * test_tc18_gaps_server.c's own busy_wait_ms, plus this same
         * helper's identically-named twin over in test_tc18_gaps_e2e.c). */
    }
}

/* As of the REQ-WDG-010 fix, rcp_mock_server_dispatch() calls
 * rcp_watchdog_keeper_kick() on every request it receives on the
 * associated stream -- mirrors test_tc18_gaps_e2e.c's own
 * test_dispatch_e2e_kicks_the_watchdog_on_every_admitted_request(): a
 * 40 ms timeout, dispatched every 10 ms for 100 ms total, far longer
 * than 40 ms would survive without kicking. rcp_server_endpoint_submit()
 * (server.h's own lower-level path, exercised directly by
 * test_tc18_gaps_server.c's own test_watchdog_overflows_despite_
 * continuous_requests()) remains deliberately out of scope -- it has no
 * stream_id concept at all to key a kick by; see
 * rcp_mock_server_set_watchdog_keeper()'s own doc comment. */
static void test_dispatch_kicks_the_watchdog_on_every_admitted_request(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_watchdog_stream_cfg_t  stream = {9u, true, 40u, true, true};
    rcp_watchdog_config_t      cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t     *k;
    const uint8_t                req[4] = {0x01, 0x02, 0x03, 0x04};
    int                           i;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, echo_handler, NULL);
    rcp_mock_server_set_watchdog_keeper(srv, k);

    for (i = 0; i < 10; i++) {
        rcp_bytes_t resp = {0};

        wdg_busy_wait_ms(10u);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
            rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                      9u, req, sizeof(req), &resp));
        TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, 9u).overflowed);

        rcp_bytes_free(&resp);
    }

    rcp_watchdog_keeper_destroy(k);
    rcp_mock_server_destroy(srv);
}

/* The "receipt not validation" half of the design, mirroring
 * test_tc18_gaps_e2e.c's own test_dispatch_e2e_kicks_the_watchdog_even_
 * when_the_request_is_rejected(): a request dispatch() goes on to
 * REJECT (same HW_UNCONFIGURED/EP0/GBB fixture as
 * test_dispatch_rejected_by_lifecycle_sends_request_rejected_error()
 * above) still means the RC Client is alive and talking on this
 * stream, so it must still kick. Spends most of the 60 ms timeout
 * before dispatching the rejected request, then most of it again after
 * -- only a kick actually caused by the rejected dispatch call (not
 * rcp_watchdog_keeper_new()'s own construction-time kick) survives
 * both waits without overflowing. */
static void test_dispatch_kicks_the_watchdog_even_when_the_request_is_rejected(void)
{
    rcp_mock_server_t           *srv = rcp_mock_server_new(); /* still HW_UNCONFIGURED */
    rcp_watchdog_stream_cfg_t    stream = {9u, true, 60u, true, true};
    rcp_watchdog_config_t        cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t       *k;
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame, resp = {0};

    hdr.byte_bus_id     = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.transaction_num = 88;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);
    rcp_mock_server_set_watchdog_keeper(srv, k);

    wdg_busy_wait_ms(40u); /* consume most of the 60 ms budget from construction */
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, 9u).overflowed);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
        rcp_mock_server_dispatch(srv, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_SUBTYPE_NTSCF,
                                  RCP_ACF_MSG_TYPE_GBB, false, 9u, frame.data, frame.len, &resp));

    wdg_busy_wait_ms(40u); /* would overflow (80 ms > 60 ms) without a kick just now */
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, 9u).overflowed); /* ... but still kicked */

    rcp_watchdog_keeper_destroy(k);
    rcp_bytes_free(&resp);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── rcp_mock_server_pwrmode_resume() ──────────────────────────────────────── */

/* REQ-PWRMODE-019 (TC18 §12.4.1): "After reception of valid message from
 * the sleep request Client all used endpoints and response queues will
 * be enabled." rcp_mock_server_pwrmode_resume() is the composition point
 * this codebase's own "pure primitive, caller composes" layering
 * (power.h deliberately never touches server.h) puts that responsibility
 * on -- every registered endpoint is re-enabled once the handshake's own
 * resume-queues step succeeds. */
static void test_pwrmode_resume_reenables_all_endpoints(void)
{
    rcp_mock_server_t      *srv = rcp_mock_server_new();
    rcp_pwrmode_handshake_t hs;

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, false, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 2, 6, false, NULL, NULL));
    to_rcp_configured(srv); /* any byte_bus_id passes lifecycle admission now --
                                see to_rcp_configured()'s own comment */

    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, true));

    /* Still disabled -- resume hasn't happened yet: a request pre-loads
     * instead of running. */
    {
        rcp_bytes_t resp = {0};
        const uint8_t req_before[] = {1, 2, 3};
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_QUEUED,
            rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false, 1u,
                                      req_before, sizeof(req_before), &resp));
        rcp_bytes_free(&resp);
    }

    TEST_ASSERT_TRUE(rcp_mock_server_pwrmode_resume(srv, &hs));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_is_complete(&hs));

    /* Both endpoints re-enabled -- confirmed on the OTHER endpoint (bus 2,
     * untouched by the pre-resume dispatch above): a request now
     * dispatches EXECUTE_NOW instead of queuing (disabled endpoints
     * pre-load, per rcp_mock_server_dispatch()'s own doc comment). */
    {
        rcp_bytes_t resp = {0};
        const uint8_t req_after[] = {4, 5, 6};
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
            rcp_mock_server_dispatch(srv, 2, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false, 1u,
                                      req_after, sizeof(req_after), &resp));
        rcp_bytes_free(&resp);
    }

    rcp_mock_server_destroy(srv);
}

/* A handshake that hasn't reached ECHOED yet (resume_queues()'s own
 * precondition) leaves every endpoint untouched -- rcp_mock_server_
 * pwrmode_resume() returns false without iterating. */
static void test_pwrmode_resume_returns_false_before_handshake_echoed(void)
{
    rcp_mock_server_t      *srv = rcp_mock_server_new();
    rcp_pwrmode_handshake_t hs;
    rcp_bytes_t             resp = {0};
    const uint8_t           req[] = {1, 2, 3};

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, false, NULL, NULL));
    to_rcp_configured(srv);

    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_FALSE(rcp_mock_server_pwrmode_resume(srv, &hs)); /* NOT_STARTED, not ECHOED */

    /* Endpoint still disabled: the request pre-loads instead of running. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_QUEUED,
        rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false, 1u,
                                  req, sizeof(req), &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_unknown_bus_after_lifecycle_accepts(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0xAA};

    /* RCP_CONFIGURED: HW_CONFIGURED admits only EP0 as of the
     * REQ-LIFECYCLE-032 fix -- see to_rcp_configured()'s own comment. */
    to_rcp_configured(srv); /* any byte_bus_id passes lifecycle admission now */

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS,
        rcp_mock_server_dispatch(srv, 7, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* REQ-TIMED-012/013: rcp_mock_server_dispatch_tscf() with tv=false
 * behaves byte-for-byte like rcp_mock_server_dispatch() itself -- a
 * standard request to an enabled endpoint still executes immediately,
 * exactly as it did (and still does, through the plain entry point)
 * before this function existed. avtp_timestamp/gptp_reference_now are
 * meaningless while tv is false (server.h's own doc comment), so
 * nonzero, otherwise-postponing values are deliberately passed here
 * to prove they really are ignored, not merely untested. */
static void test_dispatch_tscf_with_tv_false_behaves_like_plain_dispatch(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       false, 0xFFFFFFFFu, false, 0xFFFFFFFFFFu, req, sizeof(req),
                                       &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* REQ-TIMED-012/013: tv=true postpones a STANDARD request -- the exact
 * gap this pair's own text described as its own remaining scope
 * ("no real dispatch path... calls rcp_server_endpoint_admit() with a
 * real tv/avtp_timestamp/gptp_reference_now yet"). avtp_timestamp is
 * far ahead of gptp_reference_now, so the reconstructed presentation
 * instant is comfortably in the future: admission stores the request
 * (RCP_SERVER_ADMIT_PENDING) instead of running it immediately, the
 * same outcome a conditional request already gets, per server.h's own
 * REQ-TIMED-012 doc comment ("claimed into the request store exactly
 * like a conditional one"). */
static void test_dispatch_tscf_with_tv_true_postpones_a_standard_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       true, 1000000u, false, 0u, req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data); /* nothing ran -- no response yet */

    rcp_mock_server_destroy(srv);
}

/* ── §13.3 unsupported-time-sync configurable rule (REQ-AVTP-021, issue
 * #431) ─────────────────────────────────────────────────────────────────── */

/* Default policy (RCP_AVTP_TSCF_FALLBACK_DROP): unchanged from this
 * library's pre-issue-#431 behavior -- a TSCF frame with
 * time_sync_supported=false is still dropped outright, whatever tv/
 * avtp_timestamp it carries. */
static void test_dispatch_tscf_drops_without_time_sync_and_policy_is_drop(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_DROPPED,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB,
                                       false /* time_sync_supported */, 1u, true, 1000000u, false,
                                       0u, req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* RCP_AVTP_TSCF_FALLBACK_IGNORE: the SAME time_sync_supported=false/tv=
 * true/far-future avtp_timestamp inputs as the DROP test above -- no
 * longer dropped, and (TC18 §13.3's own "executed as if no presentation
 * time were included" wording) no longer postponed either: the request
 * executes immediately (RCP_MOCK_DISPATCH_OK), not RCP_MOCK_DISPATCH_
 * PENDING the way test_dispatch_tscf_with_tv_true_postpones_a_standard_
 * request() proves this exact same tv/avtp_timestamp pair produces when
 * time_sync_supported is true instead. */
static void test_dispatch_tscf_executes_immediately_without_time_sync_when_policy_is_ignore(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);
    rcp_mock_server_set_tscf_unsupported_time_sync_policy(srv, RCP_AVTP_TSCF_FALLBACK_IGNORE);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB,
                                       false /* time_sync_supported */, 1u, true, 1000000u, false,
                                       0u, req, sizeof(req), &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* time_sync_supported=true is completely unaffected by this policy --
 * rule 1's own trigger condition never holds in the first place, so
 * IGNORE changes nothing versus the ordinary (time-sync-supported)
 * behavior test_dispatch_tscf_with_tv_true_postpones_a_standard_request()
 * already pins. */
static void test_dispatch_tscf_policy_is_ignore_irrelevant_when_time_sync_supported(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);
    rcp_mock_server_set_tscf_unsupported_time_sync_policy(srv, RCP_AVTP_TSCF_FALLBACK_IGNORE);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB,
                                       true /* time_sync_supported */, 1u, true, 1000000u, false,
                                       0u, req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* ── §13.3 reserved-bytes-all-zero rule (REQ-AVTP-022, issue #431) ────────── */

/* Default policy (RCP_AVTP_TSCF_FALLBACK_DROP, the same default every
 * rcp_mock_server_t starts with): tscf_reserved_all_zero=true drops the
 * frame outright, exactly reproducing this library's pre-issue-#431
 * disposition (no such parameter existed at all before this fix) for a
 * caller that never opts in. */
static void test_dispatch_tscf_drops_when_reserved_all_zero_and_policy_is_drop(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_DROPPED,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       false, 0u, true, 0u, req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* RCP_AVTP_TSCF_FALLBACK_IGNORE: the SAME tscf_reserved_all_zero=true
 * input as the DROP test above, but now processed as if avtp_subtype had
 * genuinely been RCP_AVTP_SUBTYPE_NTSCF all along -- a standard request
 * to an enabled endpoint therefore executes immediately
 * (RCP_MOCK_DISPATCH_OK), the same outcome
 * test_dispatch_tscf_with_tv_false_behaves_like_plain_dispatch() above
 * already proves for a genuinely-NTSCF-equivalent request, isolating
 * this rule's own IGNORE-side effect from every other behavior. */
static void test_dispatch_tscf_processes_as_ntscf_when_reserved_all_zero_and_policy_is_ignore(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);
    rcp_mock_server_set_tscf_unsupported_time_sync_policy(srv, RCP_AVTP_TSCF_FALLBACK_IGNORE);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       false, 0u, true, 0u, req, sizeof(req), &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* RCP_AVTP_TSCF_FALLBACK_IGNORE's own full-NTSCF substitution overrides
 * tv too, not just avtp_subtype -- a tv=true/far-future avtp_timestamp
 * that would otherwise postpone this exact same standard request
 * (test_dispatch_tscf_with_tv_true_postpones_a_standard_request above)
 * is disregarded once tscf_reserved_all_zero triggers the substitution:
 * the request still executes immediately, not RCP_MOCK_DISPATCH_PENDING. */
static void test_dispatch_tscf_reserved_all_zero_ignore_forces_tv_false(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);
    rcp_mock_server_set_tscf_unsupported_time_sync_policy(srv, RCP_AVTP_TSCF_FALLBACK_IGNORE);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       true, 1000000u, true, 0u, req, sizeof(req), &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* tscf_reserved_all_zero=false (the ordinary, conformant case) is
 * completely unaffected by this rule, whatever policy is configured --
 * proving the rule only fires when the caller's own decoded header
 * actually earns it. */
static void test_dispatch_tscf_unaffected_when_reserved_not_all_zero(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t       req[] = {1, 2, 3};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv);
    rcp_mock_server_set_tscf_unsupported_time_sync_policy(srv, RCP_AVTP_TSCF_FALLBACK_DROP);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch_tscf(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                       false, 0u, false, 0u, req, sizeof(req), &resp));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* ── §13.3 tu=1/tu=0 equivalence (REQ-AVTP-023, issue #431) ────────────────── */

/* rcp_mock_server_dispatch_tscf() takes no tu parameter at all -- by
 * construction, there is no separate tu=1 code path for it to diverge
 * from tu=0's. This test makes that deliberate equivalence directly
 * observable: two byte-identical TSCF wire frames, differing ONLY in
 * their own tu bit, are each decoded (rcp_avtp_decode_tscf()) and then
 * dispatched through this same entry point using only the decoded
 * tv/avtp_timestamp fields (never tu) -- both produce the identical
 * outcome, proving tu=1 really is executed as if tu=0, TC18 §13.3's own
 * third rule, not merely assumed to be from tu's absence as a parameter. */
static void test_dispatch_tscf_tu_one_and_tu_zero_produce_identical_outcome(void)
{
    uint8_t                 wire_tu0[RCP_AVTP_TSCF_HEADER_LEN] = {0};
    uint8_t                 wire_tu1[RCP_AVTP_TSCF_HEADER_LEN] = {0};
    rcp_avtp_tscf_header_t  hdr_tu0, hdr_tu1;
    const uint8_t          *payload_tu0, *payload_tu1;
    size_t                  payload_len_tu0, payload_len_tu1;
    rcp_mock_server_t      *srv0 = rcp_mock_server_new();
    rcp_mock_server_t      *srv1 = rcp_mock_server_new();
    rcp_bytes_t             resp0 = {0}, resp1 = {0};
    rcp_mock_dispatch_result_t r0, r1;
    const uint8_t            req[] = {1, 2, 3};

    wire_tu0[0] = wire_tu1[0] = RCP_AVTP_SUBTYPE_TSCF;
    wire_tu0[1] = wire_tu1[1] = (uint8_t)(1u << 7); /* sv=1, tv=0 */
    wire_tu1[3]               = 0x1u;               /* tu=1 -- the only byte that differs */

    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(wire_tu0, sizeof(wire_tu0), &hdr_tu0,
                                                          &payload_tu0, &payload_len_tu0));
    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_tscf(wire_tu1, sizeof(wire_tu1), &hdr_tu1,
                                                          &payload_tu1, &payload_len_tu1));
    TEST_ASSERT_EQUAL(0u, hdr_tu0.tu);
    TEST_ASSERT_EQUAL(1u, hdr_tu1.tu); /* decode itself still tells them apart */

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv0, 1, 5, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv1, 1, 5, true, NULL, NULL));
    to_rcp_configured(srv0);
    to_rcp_configured(srv1);

    r0 = rcp_mock_server_dispatch_tscf(srv0, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                        1u, hdr_tu0.tv != 0, hdr_tu0.avtp_timestamp, false, 0u, req,
                                        sizeof(req), &resp0);
    r1 = rcp_mock_server_dispatch_tscf(srv1, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                        1u, hdr_tu1.tv != 0, hdr_tu1.avtp_timestamp, false, 0u, req,
                                        sizeof(req), &resp1);

    TEST_ASSERT_EQUAL(r0, r1);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, r0);

    rcp_bytes_free(&resp0);
    rcp_bytes_free(&resp1);
    rcp_mock_server_destroy(srv0);
    rcp_mock_server_destroy(srv1);
}

/* TC18 §12.9.1: "If the lookup of the byte_bus_id in the context of the
 * stream_id does not point to an Endpoint, the request is dropped
 * without further notification." Extends the test above with a real,
 * fully-decodable request (long enough to carry a transaction_num) to
 * confirm resp stays NULL even when a response *could* be built, not
 * just when the request is too short to build one from. Corrected
 * 2026-08-10 (c-RCP-AUDIT-06, issue #256 Group H, REQ-MOCK-030): this
 * test previously asserted the opposite -- that a Table 27 EP_NOT_FOUND
 * response was sent -- which TC18 §12.9.1 directly contradicts; Table
 * 27's EP_NOT_FOUND row is scoped to a Trigger request's own
 * trigger_source_ep naming a nonexistent EP, a different, unimplemented
 * case. */
static void test_dispatch_unknown_bus_is_dropped_silently(void)
{
    rcp_mock_server_t           *srv = rcp_mock_server_new();
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame, resp = {0};

    /* RCP_CONFIGURED -- see to_rcp_configured()'s own comment. */
    to_rcp_configured(srv); /* any byte_bus_id passes lifecycle admission now */

    hdr.byte_bus_id     = 7;
    hdr.transaction_num = 55;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS,
        rcp_mock_server_dispatch(srv, 7, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  frame.data, frame.len, &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_ok_runs_handler_immediately(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {1, 2, 3};
    int user_data_marker = 7;

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 3, 1, true /* ep_enable */, echo_handler, &user_data_marker);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 3, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_UINT(sizeof(req), g_seen_request_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, g_seen_request, sizeof(req));
    TEST_ASSERT_EQUAL_PTR(&user_data_marker, g_seen_user_data);
    TEST_ASSERT_EQUAL_UINT(sizeof(req), resp.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, resp.data, sizeof(req));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-MOCK-031 (TC18 §12.9.1, issue #432): stream-scoped byte_bus_id
 * lookup at dispatch time ───────────────────────────────────────────────
 *
 * The real proof: two endpoints sharing byte_bus_id 5, one on STREAM_A
 * and one on STREAM_B, each carrying a distinct user_data marker --
 * echo_handler() (this file's shared handler, above) records the marker
 * it was actually invoked with, so a request declaring STREAM_A must
 * reach marker_a's slot and a request declaring STREAM_B must reach
 * marker_b's slot, never the other one. A request declaring an entirely
 * different stream_id (STREAM_C, no endpoint registered for it) must be
 * dropped exactly as TC18 §12.9.1 requires for an unresolvable
 * (stream_id, byte_bus_id) lookup -- not accidentally routed to either
 * real slot. See test_add_endpoint_on_stream_allows_same_bus_id_on_
 * different_streams() etc. (above) for the registration-level half of
 * this same guarantee. */
static void test_dispatch_stream_scoped_endpoints_route_by_stream_id(void)
{
    rcp_mock_server_t *srv      = rcp_mock_server_new();
    rcp_bytes_t         resp    = {0};
    const uint8_t        req[]   = {0xAA};
    int                  marker_a = 1;
    int                  marker_b = 2;

    to_rcp_configured(srv); /* any byte_bus_id passes lifecycle admission now */

    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_A, 5, 1, true, echo_handler,
                                                &marker_a));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
        rcp_mock_server_add_endpoint_on_stream(srv, STREAM_B, 5, 1, true, echo_handler,
                                                &marker_b));

    /* A request on STREAM_A addressed at byte_bus_id 5 reaches marker_a's
     * slot, not marker_b's. */
    reset_handler_capture();
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 5, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  STREAM_A, req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_PTR(&marker_a, g_seen_user_data);
    rcp_bytes_free(&resp);

    /* The identically-addressed request on STREAM_B reaches marker_b's
     * slot instead -- same byte_bus_id, disambiguated purely by
     * stream_id. */
    reset_handler_capture();
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 5, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  STREAM_B, req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_PTR(&marker_b, g_seen_user_data);
    rcp_bytes_free(&resp);

    /* A third stream_id with no endpoint registered at byte_bus_id 5 on
     * it is dropped -- TC18 §12.9.1's own "does not point to an
     * Endpoint" case -- not silently routed to either real slot. */
    reset_handler_capture();
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS,
        rcp_mock_server_dispatch(srv, 5, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  STREAM_C, req, sizeof(req), &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* ── REQ-RMAP-048/049 (issue #334-6): response/ack routing suppression ──────
 *
 * TC18 §12.7.7 Table 24's own "0 means no X is to be sent" rule for
 * rx_ack_stream_index/rx_resp_stream_index, exercised through
 * rcp_mock_server_dispatch() itself -- not by reading the struct field
 * directly (that was already possible before this fix; the WIRING is
 * what these tests prove). */

#define RMAP048049_STREAM_ID ((uint64_t)0x0102030405060708ULL)

/* A real ACF_ABB-encoded request whose header echo_handler's own
 * rcp_bytes_dup() faithfully carries into the response -- unlike the
 * raw {1,2,3} bytes test_dispatch_ok_runs_handler_immediately uses
 * above, this is decodable by rcp_acf_classify_response() itself, the
 * same "everything but Acknowledge" (evt != 0x0F) Write-classified shape
 * REQ-RMAP-049's own rx_resp_stream_index governs. */
static rcp_bytes_t write_shaped_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t h;
    const uint8_t                pl[2] = {0xAA, 0xBB};

    memset(&h, 0, sizeof(h));
    h.byte_bus_id     = byte_bus_id;
    h.transaction_num = transaction_num;
    h.op              = (uint8_t)RCP_ACF_OP_WRITE;
    return rcp_acf_encode_abb(&h, pl, sizeof(pl));
}

/* Ignores its own request entirely and always answers with a real,
 * evt[3:0] == 0xF Acknowledge-shaped response -- proving
 * suppress_response_per_stream_cfg() (mock.c) correctly classifies and
 * suppresses an Acknowledge even though this module's own dispatch
 * pipeline has no live caller of its own that builds one yet (a
 * separate, already-known gap -- see the wiring's own doc comment in
 * mock.c). */
static void acknowledge_shaped_handler(const uint8_t *request, size_t request_len,
                                        rcp_bytes_t *out_response, void *user_data)
{
    (void)request;
    (void)request_len;
    (void)user_data;
    g_handler_called = true;
    *out_response    = rcp_acf_build_acknowledge_response((rcp_byte_bus_id_t)0x11u, 0x42u);
}

static void test_response_suppressed_when_rx_resp_stream_index_is_zero(void)
{
    rcp_mock_server_t              *srv  = rcp_mock_server_new();
    rcp_bytes_t                      req  = write_shaped_request(0x11, 0x22);
    rcp_bytes_t                      resp = {0};
    rcp_regmap_request_stream_cfg_t  cfg[1];

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, echo_handler, NULL);

    rcp_regmap_request_stream_cfg_init(&cfg[0]);
    cfg[0].rx_stream_id       = RMAP048049_STREAM_ID;
    cfg[0].rx_resp_stream_index = 0u; /* "no response is to be sent" */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, cfg, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  RMAP048049_STREAM_ID, req.data, req.len, &resp));
    /* Admission/execution are unaffected -- only the wire response is
     * suppressed; the handler still ran. */
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL_UINT(0u, resp.len);

    rcp_bytes_free(&req);
    rcp_mock_server_destroy(srv);
}

static void test_response_not_suppressed_when_rx_resp_stream_index_is_nonzero(void)
{
    rcp_mock_server_t              *srv  = rcp_mock_server_new();
    rcp_bytes_t                      req  = write_shaped_request(0x11, 0x22);
    rcp_bytes_t                      resp = {0};
    rcp_regmap_request_stream_cfg_t  cfg[1];

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, echo_handler, NULL);

    rcp_regmap_request_stream_cfg_init(&cfg[0]); /* rx_resp_stream_index defaults to 1 */
    cfg[0].rx_stream_id = RMAP048049_STREAM_ID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, cfg, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  RMAP048049_STREAM_ID, req.data, req.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL_UINT(req.len, resp.len);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&req);
    rcp_mock_server_destroy(srv);
}

/* An unresolvable stream_id (no rcp_mock_server_set_request_stream_cfg()
 * call for it at all) suppresses nothing -- the same fail-toward-no-
 * action disposition every other resolve_index() call site in mock.c
 * already uses. */
static void test_response_not_suppressed_for_unresolvable_stream(void)
{
    rcp_mock_server_t *srv  = rcp_mock_server_new();
    rcp_bytes_t         req  = write_shaped_request(0x11, 0x22);
    rcp_bytes_t         resp = {0};

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, echo_handler, NULL);
    /* Deliberately no rcp_mock_server_set_request_stream_cfg() call. */

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  RMAP048049_STREAM_ID, req.data, req.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&req);
    rcp_mock_server_destroy(srv);
}

/* rx_ack_stream_index's own TC18 default (0, "no acknowledge is to be
 * sent") suppresses an Acknowledge-shaped response -- the field
 * rx_resp_stream_index does NOT govern, proven by leaving
 * rx_resp_stream_index at its own nonzero default alongside. */
static void test_acknowledge_suppressed_by_default_ack_stream_index(void)
{
    rcp_mock_server_t              *srv  = rcp_mock_server_new();
    const uint8_t                    req[8] = {0};
    rcp_bytes_t                      resp = {0};
    rcp_regmap_request_stream_cfg_t  cfg[1];

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, acknowledge_shaped_handler, NULL);

    rcp_regmap_request_stream_cfg_init(&cfg[0]); /* rx_ack_stream_index defaults to 0 */
    cfg[0].rx_stream_id = RMAP048049_STREAM_ID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, cfg, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  RMAP048049_STREAM_ID, req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL_UINT(0u, resp.len);

    rcp_mock_server_destroy(srv);
}

/* Setting rx_ack_stream_index nonzero lets the same Acknowledge-shaped
 * response through, EVEN with rx_resp_stream_index explicitly set to 0
 * on the very same stream -- proving field separation: an Acknowledge
 * is governed only by rx_ack_stream_index, never by rx_resp_stream_index. */
static void test_acknowledge_not_suppressed_when_rx_ack_stream_index_is_nonzero(void)
{
    rcp_mock_server_t              *srv  = rcp_mock_server_new();
    const uint8_t                    req[8] = {0};
    rcp_bytes_t                      resp = {0};
    rcp_regmap_request_stream_cfg_t  cfg[1];

    to_rcp_configured(srv);
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, acknowledge_shaped_handler, NULL);

    rcp_regmap_request_stream_cfg_init(&cfg[0]);
    cfg[0].rx_stream_id         = RMAP048049_STREAM_ID;
    cfg[0].rx_ack_stream_index  = 1u; /* "send it" */
    cfg[0].rx_resp_stream_index = 0u; /* must NOT apply to an Acknowledge */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, cfg, 1));

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  RMAP048049_STREAM_ID, req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-RMAP-065/SRV-017: Flush_time heartbeat composition ─────────────────── */

static const uint8_t HEARTBEAT_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x22};

static rcp_mock_server_t *heartbeat_fixture(uint32_t flush_time_us)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new();
    rcp_regmap_response_queue_cfg_t cfg[1];

    rcp_regmap_response_queue_cfg_init(&cfg[0]);
    cfg[0].stream_uid    = 0x0001u;
    cfg[0].flush_time_us = flush_time_us;
    TEST_ASSERT_TRUE(rcp_mock_server_set_response_queue_cfg(srv, cfg, 1));
    return srv;
}

/* The very first check for a response stream only seeds this module's
 * own bookkeeping -- no previous transmission exists yet to measure
 * elapsed time against, so no heartbeat is reported even though a huge
 * now_us would otherwise look like an eternity has elapsed since a
 * zero-valued "last transmit". */
static void test_heartbeat_first_check_only_seeds_and_reports_nothing_due(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(1000u);
    rcp_bytes_t         hb  = {0};

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 999999999u, &hb));
    TEST_ASSERT_NULL(hb.data);

    rcp_mock_server_destroy(srv);
}

/* Once seeded, elapsed time below flush_time_us reports nothing due; at
 * or past it, a real, correctly composed empty NTSCF heartbeat AVTPDU
 * comes back -- the exact composition
 * test_flush_time_trigger_and_empty_heartbeat_are_composable()
 * (test_tc18_gaps_regmap.c) already proved possible, now reached through
 * one real mock.c call instead of by hand. */
static void test_heartbeat_fires_exactly_at_flush_time_and_composes_correctly(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(1000u);
    rcp_bytes_t         hb  = {0};
    rcp_avtp_ntscf_header_t decoded_hdr;
    const uint8_t           *decoded_payload;
    size_t                    decoded_payload_len;

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 0u, &hb)); /* seed */

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 999u, &hb));
    TEST_ASSERT_NULL(hb.data);

    TEST_ASSERT_TRUE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 1000u, &hb));
    TEST_ASSERT_NOT_NULL(hb.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN, hb.len);

    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK, rcp_avtp_decode_ntscf(hb.data, hb.len, &decoded_hdr,
                                                              &decoded_payload,
                                                              &decoded_payload_len));
    TEST_ASSERT_EQUAL_UINT8(1u, decoded_hdr.sv);
    TEST_ASSERT_EQUAL_UINT(0u, decoded_payload_len);
    {
        rcp_stream_id_t expected = rcp_stream_id_make(HEARTBEAT_MAC, 0x0001u);
        TEST_ASSERT_EQUAL_MEMORY(expected.mac, decoded_hdr.stream_id.mac, sizeof(expected.mac));
        TEST_ASSERT_EQUAL_UINT16(expected.unique_id, decoded_hdr.stream_id.unique_id);
    }

    rcp_bytes_free(&hb);
    rcp_mock_server_destroy(srv);
}

/* Having just fired, the flush timer is reset -- an immediately
 * following check (elapsed 0) reports nothing due again, even past a
 * SECOND full interval only from that new baseline. */
static void test_heartbeat_resets_its_own_timer_after_firing(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(1000u);
    rcp_bytes_t         hb  = {0};

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 0u, &hb)); /* seed */
    TEST_ASSERT_TRUE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 1000u, &hb));
    rcp_bytes_free(&hb);

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 1999u, &hb));
    TEST_ASSERT_NULL(hb.data);

    TEST_ASSERT_TRUE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 2000u, &hb));
    TEST_ASSERT_NOT_NULL(hb.data);

    rcp_bytes_free(&hb);
    rcp_mock_server_destroy(srv);
}

/* flush_time_us == 0 (TC18's own "flush only by count" encoding) never
 * fires a heartbeat, no matter how much time elapses -- proven past the
 * seed call with a very large now_us. */
static void test_heartbeat_never_fires_when_flush_time_is_zero(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(0u);
    rcp_bytes_t         hb  = {0};

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 0u, &hb));
    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 999999999u, &hb));
    TEST_ASSERT_NULL(hb.data);

    rcp_mock_server_destroy(srv);
}

/* A non-monotonic now_us (earlier than the last recorded transmission)
 * reports nothing due -- fails toward no heartbeat rather than firing a
 * false positive from the unsigned subtraction that would otherwise
 * underflow. */
static void test_heartbeat_non_monotonic_now_us_does_not_fire(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(1000u);
    rcp_bytes_t         hb  = {0};

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 5000u, &hb)); /* seed */
    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 1u, HEARTBEAT_MAC, 100u, &hb));
    TEST_ASSERT_NULL(hb.data);

    rcp_mock_server_destroy(srv);
}

/* response_stream_index 0 and an index beyond srv's own configured
 * response_queue_cfg_count both report nothing due -- there is no row
 * to check. */
static void test_heartbeat_out_of_range_response_stream_index_does_not_fire(void)
{
    rcp_mock_server_t *srv = heartbeat_fixture(1000u);
    rcp_bytes_t         hb  = {0};

    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 0u, HEARTBEAT_MAC, 1000000u, &hb));
    TEST_ASSERT_FALSE(
        rcp_mock_server_check_response_queue_heartbeat(srv, 2u, HEARTBEAT_MAC, 1000000u, &hb));
    TEST_ASSERT_NULL(hb.data);

    rcp_mock_server_destroy(srv);
}

/* ── REQ-WAKEUP-018: WakeUp repetition interval, resolved from Flush_time ──── */

/* rcp_regmap_request_stream_cfg_init()'s own default already sets
 * rx_resp_stream_index = 1 (REQ-RMAP-049's own documented exception to
 * "zero everything") -- combined with heartbeat_fixture()'s own
 * response_queue_cfg[0] (response_stream_index 1), the default fixture
 * below already forms a real request-stream -> response-stream chain
 * with no extra wiring needed. */
static rcp_mock_server_t *wakeup_interval_fixture(uint32_t flush_time_us)
{
    rcp_mock_server_t              *srv = heartbeat_fixture(flush_time_us);
    rcp_regmap_request_stream_cfg_t req[1];

    rcp_regmap_request_stream_cfg_init(&req[0]); /* rx_resp_stream_index defaults to 1 */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, req, 1));
    return srv;
}

static void test_wakeup_repetition_interval_resolves_via_flush_time(void)
{
    rcp_mock_server_t *srv = wakeup_interval_fixture(5000u);
    uint32_t            interval = 0xEEEEEEEEu;

    TEST_ASSERT_TRUE(rcp_mock_server_wakeup_repetition_interval_us(srv, 1u, &interval));
    TEST_ASSERT_EQUAL_UINT32(5000u, interval);

    rcp_mock_server_destroy(srv);
}

static void test_wakeup_repetition_interval_out_of_range_request_stream_index(void)
{
    rcp_mock_server_t *srv = wakeup_interval_fixture(5000u);
    uint32_t            interval = 0xEEEEEEEEu;

    TEST_ASSERT_FALSE(rcp_mock_server_wakeup_repetition_interval_us(srv, 0u, &interval));
    TEST_ASSERT_EQUAL_UINT32(0u, interval);

    interval = 0xEEEEEEEEu;
    TEST_ASSERT_FALSE(rcp_mock_server_wakeup_repetition_interval_us(srv, 2u, &interval));
    TEST_ASSERT_EQUAL_UINT32(0u, interval);

    rcp_mock_server_destroy(srv);
}

/* An rx_resp_stream_index that does not resolve to a real
 * response_queue_cfg[] row (here: no response-queue table configured at
 * all, so even the default rx_resp_stream_index == 1 is out of range)
 * fails the same way as an out-of-range request_stream_index -- the
 * identical "not a real row" convention rcp_mock_server_check_response_
 * queue_heartbeat() already uses for its own response_stream_index. */
static void test_wakeup_repetition_interval_unresolvable_response_stream_fails(void)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new();
    rcp_regmap_request_stream_cfg_t req[1];
    uint32_t                        interval = 0xEEEEEEEEu;

    rcp_regmap_request_stream_cfg_init(&req[0]); /* rx_resp_stream_index == 1, but no
                                                      response_queue_cfg row exists */
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, req, 1));

    TEST_ASSERT_FALSE(rcp_mock_server_wakeup_repetition_interval_us(srv, 1u, &interval));
    TEST_ASSERT_EQUAL_UINT32(0u, interval);

    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-046: watchdog cause, rcp_mock_server_check_watchdog() ─────────── */

static rcp_mock_server_t *watchdog_fixture(bool wd_enable, uint32_t wd_timeout_ms,
                                            bool wd_safestate_enable, bool wd_info_enable)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new();
    rcp_regmap_request_stream_cfg_t req[1];

    rcp_regmap_request_stream_cfg_init(&req[0]);
    req[0].rx_wd_enable           = wd_enable;
    req[0].rx_wd_timeout_ms       = wd_timeout_ms;
    req[0].rx_wd_safestate_enable = wd_safestate_enable;
    req[0].rx_wd_info_enable      = wd_info_enable;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, req, 1));
    return srv;
}

static void test_watchdog_overflow_latches_stream_status(void)
{
    rcp_mock_server_t   *srv = watchdog_fixture(true, 1000u, true, false);
    rcp_e2e_wd_result_t   result;

    TEST_ASSERT_TRUE(rcp_mock_server_check_watchdog(srv, 1u, 1000u, &result));
    TEST_ASSERT_TRUE(result.overflowed);
    TEST_ASSERT_TRUE(result.enter_safe_state);
    TEST_ASSERT_FALSE(result.notify);
    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, 0)); /* stream_id unused by
                                                                            resolve_index() when
                                                                            no rx_stream_id was
                                                                            set -- see below */

    rcp_mock_server_destroy(srv);
}

static void test_watchdog_below_timeout_does_not_overflow(void)
{
    rcp_mock_server_t   *srv = watchdog_fixture(true, 1000u, true, false);
    rcp_e2e_wd_result_t   result;

    TEST_ASSERT_TRUE(rcp_mock_server_check_watchdog(srv, 1u, 999u, &result));
    TEST_ASSERT_FALSE(result.overflowed);
    TEST_ASSERT_FALSE(result.enter_safe_state);

    rcp_mock_server_destroy(srv);
}

static void test_watchdog_disabled_never_overflows(void)
{
    rcp_mock_server_t   *srv = watchdog_fixture(false, 1000u, true, true);
    rcp_e2e_wd_result_t   result;

    TEST_ASSERT_TRUE(rcp_mock_server_check_watchdog(srv, 1u, 999999u, &result));
    TEST_ASSERT_FALSE(result.overflowed);
    TEST_ASSERT_FALSE(result.enter_safe_state);
    TEST_ASSERT_FALSE(result.notify);

    rcp_mock_server_destroy(srv);
}

/* rx_wd_safestate_enable == false: an overflow still fires notify (if
 * rx_wd_info_enable), but never latches stream_status[]'s own wd cause --
 * rcp_e2e_stream_status_note_wd() only ever latches on enter_safe_state,
 * exactly like the other three causes' own identical rule. This is the
 * scenario rcp_mock_server_stream_status_rx_blocked() alone could never
 * distinguish from "nothing happened" -- *out_result is why this function
 * returns the full result, not just the latched bit. */
static void test_watchdog_notify_without_safestate_does_not_latch(void)
{
    rcp_mock_server_t   *srv = watchdog_fixture(true, 1000u, false, true);
    rcp_e2e_wd_result_t   result;

    TEST_ASSERT_TRUE(rcp_mock_server_check_watchdog(srv, 1u, 1000u, &result));
    TEST_ASSERT_TRUE(result.overflowed);
    TEST_ASSERT_FALSE(result.enter_safe_state);
    TEST_ASSERT_TRUE(result.notify);
    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, 0));

    rcp_mock_server_destroy(srv);
}

static void test_watchdog_out_of_range_request_stream_index_returns_false(void)
{
    rcp_mock_server_t   *srv = watchdog_fixture(true, 1000u, true, true);
    rcp_e2e_wd_result_t   result;

    memset(&result, 0xEE, sizeof(result));
    TEST_ASSERT_FALSE(rcp_mock_server_check_watchdog(srv, 0u, 999999u, &result));
    TEST_ASSERT_FALSE(result.overflowed);
    TEST_ASSERT_FALSE(result.enter_safe_state);
    TEST_ASSERT_FALSE(result.notify);

    memset(&result, 0xEE, sizeof(result));
    TEST_ASSERT_FALSE(rcp_mock_server_check_watchdog(srv, 2u, 999999u, &result));
    TEST_ASSERT_FALSE(result.overflowed);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_no_handler_leaves_response_zeroed(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0x01};

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    rcp_mock_server_add_endpoint(srv, 4, 1, true, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 4, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL_UINT(0, resp.len);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_queued_when_endpoint_disabled(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0x01};

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 5, 1, false /* ep_enable */, echo_handler, NULL);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_QUEUED,
        rcp_mock_server_dispatch(srv, 5, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL_UINT(1, rcp_mock_server_endpoint_queue_len(srv, 5));

    rcp_mock_server_destroy(srv);
}

static void test_drain_endpoint_runs_queued_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {9, 8, 7};

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 6, 1, false, echo_handler, NULL);
    rcp_mock_server_dispatch(srv, 6, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u,
                              req, sizeof(req), &resp);
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_mock_server_set_endpoint_enable(srv, 6, true);
    TEST_ASSERT_TRUE(rcp_mock_server_drain_endpoint(srv, 6, &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_UINT(sizeof(req), g_seen_request_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, g_seen_request, sizeof(req));
    TEST_ASSERT_EQUAL_UINT(0, rcp_mock_server_endpoint_queue_len(srv, 6));

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

static void test_drain_endpoint_empty_queue_returns_false(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};

    rcp_mock_server_add_endpoint(srv, 8, 1, true, NULL, NULL);
    TEST_ASSERT_FALSE(rcp_mock_server_drain_endpoint(srv, 8, &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

static void test_drain_endpoint_unknown_bus_returns_false(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};

    TEST_ASSERT_FALSE(rcp_mock_server_drain_endpoint(srv, 99, &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

static void test_discovery_bus_accepted_while_hw_unconfigured(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0x00};

    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, 1, true, echo_handler, NULL);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, RCP_AVTP_SUBTYPE_NTSCF,
                                  RCP_ACF_MSG_TYPE_ABB, true, 1u, req, sizeof(req), &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* ── Multi-request-per-frame dispatch (TC18 §12.9.1.1) ──────────────────────── */

static void test_dispatch_frame_dispatches_each_member_to_its_own_endpoint(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_acf_byte_message_info_t hdr1 = {0};
    rcp_acf_byte_message_info_t hdr2 = {0};
    uint8_t                     body1[] = {0xAA, 0xBB};
    uint8_t                     body2[] = {0xCC, 0xDD, 0xEE};
    rcp_bytes_t                 frame1, frame2;
    uint8_t                     combined[64];
    size_t                      combined_len;
    rcp_mock_frame_member_result_t results[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t                      dispatched;

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 10, 1, true, echo_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 20, 1, true, echo_handler, NULL);

    hdr1.byte_bus_id = 10;
    hdr2.byte_bus_id = 20;
    frame1 = rcp_acf_encode_abb(&hdr1, body1, sizeof(body1));
    frame2 = rcp_acf_encode_abb(&hdr2, body2, sizeof(body2));
    TEST_ASSERT_NOT_NULL(frame1.data);
    TEST_ASSERT_NOT_NULL(frame2.data);
    TEST_ASSERT_TRUE(frame1.len + frame2.len <= sizeof(combined));

    memcpy(combined, frame1.data, frame1.len);
    memcpy(combined + frame1.len, frame2.data, frame2.len);
    combined_len = frame1.len + frame2.len;

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, combined,
                                                 combined_len, results, RCP_MOCK_MAX_FRAME_MEMBERS);

    TEST_ASSERT_EQUAL_UINT(2, dispatched);

    /* echo_handler echoes back the exact request bytes it was handed --
     * the whole raw ACF member (header+payload+pad), matching
     * rcp_mock_server_dispatch()'s own request/request_len convention
     * (mock.h): "request_len is one already-framed request", not just
     * its payload. */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    TEST_ASSERT_EQUAL_UINT8(10, results[0].byte_bus_id);
    TEST_ASSERT_EQUAL_UINT(frame1.len, results[0].response.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame1.data, results[0].response.data, frame1.len);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[1].result);
    TEST_ASSERT_EQUAL_UINT8(20, results[1].byte_bus_id);
    TEST_ASSERT_EQUAL_UINT(frame2.len, results[1].response.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame2.data, results[1].response.data, frame2.len);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&frame1);
    rcp_bytes_free(&frame2);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_frame_single_member_matches_direct_dispatch(void)
{
    /* A single-member frame behaves identically to calling
     * rcp_mock_server_dispatch() once directly -- see mock.h's file
     * header. */
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     body[] = {1, 2, 3, 4};
    rcp_bytes_t                 frame;
    rcp_mock_frame_member_result_t results[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t                      dispatched;

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    reset_handler_capture();
    rcp_mock_server_add_endpoint(srv, 11, 1, true, echo_handler, NULL);

    hdr.byte_bus_id = 11;
    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, frame.data,
                                                 frame.len, results, RCP_MOCK_MAX_FRAME_MEMBERS);

    TEST_ASSERT_EQUAL_UINT(1, dispatched);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    TEST_ASSERT_EQUAL_UINT8(11, results[0].byte_bus_id);
    /* echo_handler echoes the whole raw ACF member -- see the identical
     * note in test_dispatch_frame_dispatches_each_member_to_its_own_endpoint. */
    TEST_ASSERT_EQUAL_UINT(frame.len, results[0].response.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, results[0].response.data, frame.len);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_frame_returns_zero_for_unparseable_frame(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    const uint8_t       garbage[3] = {0xFF, 0xFF, 0xFF};
    rcp_mock_frame_member_result_t results[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t              dispatched;

    to_hw_configured(srv);

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, garbage,
                                                 sizeof(garbage), results, RCP_MOCK_MAX_FRAME_MEMBERS);
    TEST_ASSERT_EQUAL_UINT(0, dispatched);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_frame_reports_unknown_bus_for_undecodable_member(void)
{
    /* A syntactically well-formed member (rcp_sched_split_frame_members()
     * accepts it -- msg_type=ABB, declared length self-consistent with
     * the buffer) whose own pad count exceeds its declared body region
     * (pad=1 but acf_msg_length leaves 0 body octets) cannot decode
     * through rcp_acf_decode_abb() (RCP_ACF_ERR_SHORT_FRAME, acf.h) --
     * this must surface as RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS, not a
     * crash or a bogus dispatch. (This used to use an out-of-range
     * byte_bus_id as its undecodable stimulus; REQ-RMAP-053/REQ-ACF-020
     * widened rcp_byte_bus_id_t to the full 11-bit wire range, so that
     * stimulus no longer fails to decode -- an over-declared pad count
     * is a still-valid, unrelated way for a member to be undecodable.
     * A msg_type the splitter itself doesn't recognize was tried first
     * and rejected: rcp_sched_split_frame_members() (scheduler.c) only
     * accepts ACF_ABB/ACF_GBB at the framing level, so that member never
     * even reaches per-member decode -- dispatched would be 0, not the
     * 1-dispatched-but-undecodable scenario this test needs.) */
    rcp_mock_server_t *srv = rcp_mock_server_new();
    uint8_t             raw[RCP_ACF_ABB_HEADER_LEN];
    rcp_mock_frame_member_result_t results[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t              dispatched;

    to_hw_configured(srv);

    memset(raw, 0, sizeof(raw));
    raw[0] = (uint8_t)(RCP_ACF_MSG_TYPE_ABB << 1) | 0x00u; /* type=ABB, len MSB=0 */
    raw[1] = (uint8_t)(RCP_ACF_ABB_HEADER_LEN / 4u);        /* len=2 quadlets: 0 body octets */
    raw[2] = 0x40u; /* pad[7:6] = 01 -> pad=1, but body_len computes to 0 */

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, raw,
                                                 sizeof(raw), results, RCP_MOCK_MAX_FRAME_MEMBERS);

    TEST_ASSERT_EQUAL_UINT(1, dispatched);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS, results[0].result);
    TEST_ASSERT_EQUAL_UINT8(0, results[0].byte_bus_id);
    TEST_ASSERT_NULL(results[0].response.data);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_frame_truncates_at_out_cap(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_acf_byte_message_info_t hdr1 = {0};
    rcp_acf_byte_message_info_t hdr2 = {0};
    rcp_bytes_t                 frame1, frame2;
    uint8_t                     combined[64];
    size_t                      combined_len;
    rcp_mock_frame_member_result_t results[1];
    size_t                      dispatched;

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    rcp_mock_server_add_endpoint(srv, 30, 1, true, NULL, NULL);
    rcp_mock_server_add_endpoint(srv, 31, 1, true, NULL, NULL);

    hdr1.byte_bus_id = 30;
    hdr2.byte_bus_id = 31;
    frame1 = rcp_acf_encode_abb(&hdr1, NULL, 0);
    frame2 = rcp_acf_encode_abb(&hdr2, NULL, 0);

    memcpy(combined, frame1.data, frame1.len);
    memcpy(combined + frame1.len, frame2.data, frame2.len);
    combined_len = frame1.len + frame2.len;

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, combined,
                                                 combined_len, results, 1);

    TEST_ASSERT_EQUAL_UINT(1, dispatched); /* only out_cap's worth, not both */
    TEST_ASSERT_EQUAL_UINT8(30, results[0].byte_bus_id);

    rcp_bytes_free(&frame1);
    rcp_bytes_free(&frame2);
    rcp_mock_server_destroy(srv);
}

/* ── Discovery-stream claim (REQ-RMAP-066, issue #336) ─────────────────────── */

/* A new server's discovery_claim.timeout_ms already reflects TC18's own
 * stated svr_discovery_timeout default (20000 us -> 20 ms) -- proves
 * rcp_mock_server_new()'s own internal call to
 * rcp_mock_server_set_discovery_timeout_us() actually ran, not just
 * that the accessor returns a non-NULL pointer. */
static void test_new_server_discovery_claim_starts_with_the_tc18_default_timeout(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL_UINT16(20000u, rcp_mock_server_svr_ep_cfg(srv)->svr_discovery_timeout);
    TEST_ASSERT_EQUAL_UINT32(20u, rcp_mock_server_discovery_claim(srv)->timeout_ms);
    TEST_ASSERT_FALSE(rcp_mock_server_discovery_claim(srv)->held); /* fresh, unheld */

    rcp_mock_server_destroy(srv);
}

/* rcp_mock_server_set_discovery_timeout_us() keeps svr_ep_cfg and
 * discovery_claim in sync -- the same capacity-sync convention
 * REQ-RMAP-032/034/036/037 already established for other tables --
 * with a truncating (not rounding) us->ms conversion. */
static void test_set_discovery_timeout_us_syncs_svr_ep_cfg_and_claim(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_NOT_NULL(srv);

    rcp_mock_server_set_discovery_timeout_us(srv, 5500u); /* 5.5 ms -> truncates to 5 */
    TEST_ASSERT_EQUAL_UINT16(5500u, rcp_mock_server_svr_ep_cfg(srv)->svr_discovery_timeout);
    TEST_ASSERT_EQUAL_UINT32(5u, rcp_mock_server_discovery_claim(srv)->timeout_ms);

    rcp_mock_server_set_discovery_timeout_us(srv, 999u); /* < 1 ms -> truncates to 0 */
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_mock_server_discovery_claim(srv)->timeout_ms);

    rcp_mock_server_destroy(srv);
}

/* The real discovery.h claim lifecycle (open -> note_request -> lapse ->
 * open again), genuinely driven end-to-end by srv's own configured
 * svr_discovery_timeout -- not a re-implementation of discovery.h's own
 * logic, the actual rcp_discovery_claim_* functions operating on
 * srv->discovery_claim via rcp_mock_server_discovery_claim(). Proves
 * the wiring is real, not merely that the two fields happen to hold
 * matching numbers. */
static void test_discovery_claim_lifecycle_driven_by_configured_timeout(void)
{
    rcp_mock_server_t     *srv = rcp_mock_server_new();
    rcp_discovery_claim_t *claim;
    rcp_stream_id_t         requester_a = {{1, 2, 3, 4, 5, 6}, 1u};
    rcp_stream_id_t         requester_b = {{7, 8, 9, 10, 11, 12}, 2u};

    TEST_ASSERT_NOT_NULL(srv);
    rcp_mock_server_set_discovery_timeout_us(srv, 10000u); /* 10 ms, a short, test-friendly window */
    claim = rcp_mock_server_discovery_claim(srv);

    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(claim, 0u)); /* never held -- open */
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_request(claim, requester_a, 0u));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_open(claim, 5u)); /* well within the 10 ms window */
    TEST_ASSERT_FALSE(rcp_discovery_claim_note_request(claim, requester_b, 5u)); /* a second
                                                                                     requester is
                                                                                     refused,
                                                                                     REQ-DISC-029 */
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(claim, 10u)); /* the window has now lapsed */
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_request(claim, requester_b, 10u)); /* re-grantable
                                                                                     once open */

    rcp_mock_server_destroy(srv);
}

/* ── EP_generic_cfg live view (REQ-RMAP-036, issue #334) ─────────────────────── */

/* rcp_mock_server_ep_generic_cfg_view() gathers exactly the live,
 * in_use endpoints' own generic-cfg content -- not a stale or
 * default-initialized copy. */
static void test_ep_generic_cfg_view_gathers_live_in_use_slots_only(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_regmap_ep_generic_cfg_t view[4];
    size_t                       total;
    size_t                       i;

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                       rcp_mock_server_add_endpoint(srv, 10u, 3u, true, NULL, NULL)); /* slot 0 */
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                       rcp_mock_server_add_endpoint(srv, 20u, 6u, true, NULL, NULL)); /* slot 1 */
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                       rcp_mock_server_add_endpoint(srv, 30u, 9u, true, NULL, NULL)); /* slot 2 */
    /* Removing the MIDDLE endpoint, with no further add_endpoint() call
     * afterward, leaves a genuine hole at slot 1 for the view() call
     * below to skip over -- unlike removing-then-immediately-re-adding
     * (which reuses the freed slot and never actually exercises the
     * in_use skip). */
    TEST_ASSERT_TRUE(rcp_mock_server_remove_endpoint(srv, 20u));

    total = rcp_mock_server_ep_generic_cfg_view(srv, view, 4u);
    TEST_ASSERT_EQUAL(2u, total); /* 10 and 30 only -- 20 was removed, slot 1 skipped */

    for (i = 0; i < total; i++) {
        TEST_ASSERT_NOT_EQUAL(6u, view[i].ep_type); /* the removed endpoint's own ep_type never
                                                         appears -- slot 1 was genuinely skipped,
                                                         not just coincidentally absent */
    }
    TEST_ASSERT_TRUE((view[0].ep_type == 3u && view[1].ep_type == 9u) ||
                      (view[0].ep_type == 9u && view[1].ep_type == 3u));

    rcp_mock_server_destroy(srv);
}

/* rcp_mock_server_apply_ep_generic_cfg() rejects a count that doesn't
 * exactly match srv->endpoint_count, leaving every slot untouched --
 * the one invariant this function's own doc comment says it enforces,
 * since a mismatched count would otherwise silently misattribute rows. */
static void test_apply_ep_generic_cfg_rejects_mismatched_count(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_regmap_ep_generic_cfg_t entries[1];

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 10u, 3u, true, NULL, NULL));
    rcp_regmap_ep_generic_cfg_init(&entries[0]);
    entries[0].ep_type = 99u;

    TEST_ASSERT_FALSE(rcp_mock_server_apply_ep_generic_cfg(srv, entries, 0u)); /* endpoint_count
                                                                                   is 1, not 0 */
    TEST_ASSERT_FALSE(rcp_mock_server_apply_ep_generic_cfg(srv, entries, 2u));

    rcp_mock_server_destroy(srv);
}

/* rcp_mock_server_apply_ep_generic_cfg() scatters into the correct
 * live slots even with a genuine hole in the slot array -- same
 * "removed, not re-added" setup test_ep_generic_cfg_view_gathers_live_
 * in_use_slots_only() above uses, so the in_use skip in THIS
 * function's own loop is actually exercised too (a hole-free setup
 * would pass even with that skip removed entirely). */
static void test_apply_ep_generic_cfg_scatters_correctly_around_a_hole(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_regmap_ep_generic_cfg_t view[4];
    size_t                       count;
    size_t                       i;

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 10u, 3u, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 20u, 6u, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 30u, 9u, true, NULL, NULL));
    TEST_ASSERT_TRUE(rcp_mock_server_remove_endpoint(srv, 20u)); /* genuine hole, not re-filled */

    count = rcp_mock_server_ep_generic_cfg_view(srv, view, 4u);
    TEST_ASSERT_EQUAL(2u, count);
    for (i = 0; i < count; i++) view[i].ep_delay_time = 50u; /* mark every gathered row */

    TEST_ASSERT_TRUE(rcp_mock_server_apply_ep_generic_cfg(srv, view, count));

    /* Both surviving live endpoints picked up the marked value -- the
     * hole at the removed slot was correctly skipped on the way back
     * in, not overwritten or shifted into. */
    {
        rcp_regmap_ep_generic_cfg_t after[4];
        size_t                       after_count = rcp_mock_server_ep_generic_cfg_view(srv, after, 4u);

        TEST_ASSERT_EQUAL(2u, after_count);
        for (i = 0; i < after_count; i++) {
            TEST_ASSERT_EQUAL_UINT32(50u, after[i].ep_delay_time);
        }
    }

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-036: a real write applied through the ACTUAL EP0 dispatcher
 * (rcp_regmap_ep0_decode_write_request()), against a gathered view,
 * genuinely lands back in the live endpoint slot once scattered --
 * closing the same gap PR F/H already closed for the other pointed-to
 * tables: proving the wire codec, not that mock.c's own production
 * dispatch loop calls it (that stays deliberately unwired, matching
 * every sibling table's own disposition). ep_tx_buffer_size (relative
 * 0x0008, 16 bit, R/W*) is the field under test -- distinct from
 * ep_type (relative 0x0000, plain R, silently no-op on write, would
 * prove nothing about a real applied change). */
static void test_ep_generic_cfg_write_round_trips_through_the_real_dispatcher(void)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new();
    rcp_regmap_ep_generic_cfg_t     view[4];
    size_t                          count;
    rcp_regmap_general_t            map;
    rcp_lifecycle_writer_ctx_t      discovery_writer = {false, false, false, true};
    rcp_acf_byte_message_info_t     hdr = {0};
    rcp_bytes_t                     frame;
    rcp_wire_error_t                err;
    uint8_t                         tn = 0;
    rcp_regmap_ep0_errc_t           rc;
    uint8_t                         payload[4];

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 10u, 3u, true, NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint(srv, 20u, 6u, true, NULL, NULL));

    count = rcp_mock_server_ep_generic_cfg_view(srv, view, 4u);
    TEST_ASSERT_EQUAL(2u, count);

    rcp_regmap_general_init(&map);
    map.svr_ep_generic_cfg_ptr = 0x0500u; /* arbitrary, matching the sibling dispatcher
                                              test's own convention */

    hdr.byte_bus_id     = RCP_REGMAP_EP0_INDEX;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.transaction_num = 41;

    /* addr = 0x0500 + 12 (row 1's own start) + 8 (ep_tx_buffer_size's
     * own relative offset) = 0x0514. Whichever live slot landed at
     * view[1] is the one this write targets. */
    payload[0] = 0x05u;
    payload[1] = 0x14u;
    payload[2] = 0x12u;
    payload[3] = 0x34u; /* new ep_tx_buffer_size = 0x1234 words */

    frame = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);
    rc = rcp_regmap_ep0_decode_write_request(frame.data, frame.len, &map,
                                               RCP_LIFECYCLE_HW_UNCONFIGURED, discovery_writer,
                                               NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u, view, count,
                                               NULL, NULL, 0u, 0u, &err, &tn, 0u, NULL, NULL, 0u, 0u);
    TEST_ASSERT_EQUAL(RCP_REGMAP_EP0_OK, rc);
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    TEST_ASSERT_EQUAL_UINT16(0x1234u, view[1].ep_tx_buffer_size); /* the dispatcher mutated the
                                                                      gathered array in place */
    rcp_bytes_free(&frame);

    /* Scatter back -- the live slot that produced view[1] now holds
     * the applied change too. */
    TEST_ASSERT_TRUE(rcp_mock_server_apply_ep_generic_cfg(srv, view, count));
    {
        rcp_regmap_ep_generic_cfg_t after[4];
        size_t                       after_count = rcp_mock_server_ep_generic_cfg_view(srv, after, 4u);
        bool                         found = false;
        size_t                       i;

        TEST_ASSERT_EQUAL(2u, after_count);
        for (i = 0; i < after_count; i++) {
            if (after[i].ep_tx_buffer_size == 0x1234u) found = true;
        }
        TEST_ASSERT_TRUE(found);
    }

    rcp_mock_server_destroy(srv);
}

/* ── Error strings ─────────────────────────────────────────────────────────── */

static void test_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_mock_strerror(RCP_MOCK_OK));
    TEST_ASSERT_NOT_NULL(rcp_mock_strerror(RCP_MOCK_ERR_DUPLICATE_BUS_ID));
    TEST_ASSERT_NOT_NULL(rcp_mock_strerror(RCP_MOCK_ERR_CAPACITY));
    TEST_ASSERT_NOT_NULL(rcp_mock_strerror(RCP_MOCK_ERR_NOT_FOUND));
    TEST_ASSERT_NOT_NULL(rcp_mock_strerror((rcp_mock_errc_t)999));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_new_server_starts_hw_unconfigured);
    RUN_TEST(test_transition_passthrough_valid);
    RUN_TEST(test_transition_passthrough_rejects_invalid);
    RUN_TEST(test_destroy_null_is_safe);

    RUN_TEST(test_new_server_regmap_starts_empty);
    RUN_TEST(test_regmap_is_mutable);

    RUN_TEST(test_add_endpoint_increments_svr_ep_count);
    RUN_TEST(test_add_endpoint_duplicate_bus_id_rejected);
    RUN_TEST(test_add_endpoint_capacity_exhausted);
    RUN_TEST(test_add_endpoint_on_stream_allows_same_bus_id_on_different_streams);
    RUN_TEST(test_add_endpoint_on_stream_duplicate_on_same_stream_rejected);
    RUN_TEST(test_add_endpoint_on_stream_rejected_when_unscoped_endpoint_already_registered);
    RUN_TEST(test_remove_endpoint_decrements_svr_ep_count);
    RUN_TEST(test_remove_endpoint_unknown_bus_returns_false);
    RUN_TEST(test_readd_after_remove_succeeds);
    RUN_TEST(test_set_endpoint_enable_unknown_bus_returns_false);
    RUN_TEST(test_queue_len_unknown_bus_is_zero);
    RUN_TEST(test_dispatch_tscf_with_tv_false_behaves_like_plain_dispatch);
    RUN_TEST(test_dispatch_tscf_with_tv_true_postpones_a_standard_request);
    RUN_TEST(test_dispatch_tscf_drops_without_time_sync_and_policy_is_drop);
    RUN_TEST(test_dispatch_tscf_executes_immediately_without_time_sync_when_policy_is_ignore);
    RUN_TEST(test_dispatch_tscf_policy_is_ignore_irrelevant_when_time_sync_supported);
    RUN_TEST(test_dispatch_tscf_drops_when_reserved_all_zero_and_policy_is_drop);
    RUN_TEST(test_dispatch_tscf_processes_as_ntscf_when_reserved_all_zero_and_policy_is_ignore);
    RUN_TEST(test_dispatch_tscf_reserved_all_zero_ignore_forces_tv_false);
    RUN_TEST(test_dispatch_tscf_unaffected_when_reserved_not_all_zero);
    RUN_TEST(test_dispatch_tscf_tu_one_and_tu_zero_produce_identical_outcome);

    RUN_TEST(test_dispatch_dropped_by_lifecycle);
    RUN_TEST(test_dispatch_rejected_by_lifecycle_sends_request_rejected_error);
    RUN_TEST(test_dispatch_kicks_the_watchdog_on_every_admitted_request);
    RUN_TEST(test_dispatch_kicks_the_watchdog_even_when_the_request_is_rejected);
    RUN_TEST(test_pwrmode_resume_reenables_all_endpoints);
    RUN_TEST(test_pwrmode_resume_returns_false_before_handshake_echoed);
    RUN_TEST(test_dispatch_unknown_bus_after_lifecycle_accepts);
    RUN_TEST(test_dispatch_unknown_bus_is_dropped_silently);
    RUN_TEST(test_dispatch_ok_runs_handler_immediately);
    RUN_TEST(test_dispatch_stream_scoped_endpoints_route_by_stream_id);
    RUN_TEST(test_response_suppressed_when_rx_resp_stream_index_is_zero);
    RUN_TEST(test_response_not_suppressed_when_rx_resp_stream_index_is_nonzero);
    RUN_TEST(test_response_not_suppressed_for_unresolvable_stream);
    RUN_TEST(test_acknowledge_suppressed_by_default_ack_stream_index);
    RUN_TEST(test_acknowledge_not_suppressed_when_rx_ack_stream_index_is_nonzero);
    RUN_TEST(test_heartbeat_first_check_only_seeds_and_reports_nothing_due);
    RUN_TEST(test_heartbeat_fires_exactly_at_flush_time_and_composes_correctly);
    RUN_TEST(test_heartbeat_resets_its_own_timer_after_firing);
    RUN_TEST(test_heartbeat_never_fires_when_flush_time_is_zero);
    RUN_TEST(test_heartbeat_non_monotonic_now_us_does_not_fire);
    RUN_TEST(test_heartbeat_out_of_range_response_stream_index_does_not_fire);

    RUN_TEST(test_wakeup_repetition_interval_resolves_via_flush_time);
    RUN_TEST(test_wakeup_repetition_interval_out_of_range_request_stream_index);
    RUN_TEST(test_wakeup_repetition_interval_unresolvable_response_stream_fails);

    RUN_TEST(test_watchdog_overflow_latches_stream_status);
    RUN_TEST(test_watchdog_below_timeout_does_not_overflow);
    RUN_TEST(test_watchdog_disabled_never_overflows);
    RUN_TEST(test_watchdog_notify_without_safestate_does_not_latch);
    RUN_TEST(test_watchdog_out_of_range_request_stream_index_returns_false);
    RUN_TEST(test_dispatch_no_handler_leaves_response_zeroed);
    RUN_TEST(test_dispatch_queued_when_endpoint_disabled);
    RUN_TEST(test_drain_endpoint_runs_queued_request);
    RUN_TEST(test_drain_endpoint_empty_queue_returns_false);
    RUN_TEST(test_drain_endpoint_unknown_bus_returns_false);
    RUN_TEST(test_discovery_bus_accepted_while_hw_unconfigured);

    RUN_TEST(test_dispatch_frame_dispatches_each_member_to_its_own_endpoint);
    RUN_TEST(test_dispatch_frame_single_member_matches_direct_dispatch);
    RUN_TEST(test_dispatch_frame_returns_zero_for_unparseable_frame);
    RUN_TEST(test_dispatch_frame_reports_unknown_bus_for_undecodable_member);
    RUN_TEST(test_dispatch_frame_truncates_at_out_cap);

    RUN_TEST(test_new_server_discovery_claim_starts_with_the_tc18_default_timeout);
    RUN_TEST(test_set_discovery_timeout_us_syncs_svr_ep_cfg_and_claim);
    RUN_TEST(test_discovery_claim_lifecycle_driven_by_configured_timeout);

    RUN_TEST(test_ep_generic_cfg_view_gathers_live_in_use_slots_only);
    RUN_TEST(test_apply_ep_generic_cfg_rejects_mismatched_count);
    RUN_TEST(test_apply_ep_generic_cfg_scatters_correctly_around_a_hole);
    RUN_TEST(test_ep_generic_cfg_write_round_trips_through_the_real_dispatcher);

    RUN_TEST(test_strerror_never_null);

    return UNITY_END();
}
