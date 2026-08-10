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
/* Tests the TC18-shaped RC-Server/endpoint test double (ROADMAP.md
 * milestone 77). The pre-TC18 zone-controller mock this file used to test
 * moved to tests/legacy_mock.h/.c; tests/test_legacy_mock.c (a renamed
 * copy of this file's own old content) now tests that instead. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

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
        rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, true,
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
                                  RCP_ACF_MSG_TYPE_GBB, false, frame.data, frame.len, &resp));
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
            rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false,
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
            rcp_mock_server_dispatch(srv, 2, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false,
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
        rcp_mock_server_dispatch(srv, 1, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, false,
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
        rcp_mock_server_dispatch(srv, 7, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* TC18 Table 27: EP_NOT_FOUND (8). Extends the test above with a real,
 * fully-decodable request (long enough to carry a transaction_num) to
 * check the actual response, not just that resp stays NULL for a
 * too-short one. */
static void test_dispatch_unknown_bus_sends_ep_not_found_error(void)
{
    rcp_mock_server_t           *srv = rcp_mock_server_new();
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_bytes_t                  frame, resp = {0};
    rcp_acf_byte_message_info_t  resp_hdr;
    const uint8_t                *payload;
    size_t                        payload_len;

    /* RCP_CONFIGURED -- see to_rcp_configured()'s own comment. */
    to_rcp_configured(srv); /* any byte_bus_id passes lifecycle admission now */

    hdr.byte_bus_id     = 7;
    hdr.transaction_num = 55;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS,
        rcp_mock_server_dispatch(srv, 7, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  frame.data, frame.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &resp_hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&resp_hdr));
    TEST_ASSERT_EQUAL_UINT8(7u, resp_hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(55u, resp_hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_EP_NOT_FOUND, payload[0]);

    rcp_bytes_free(&resp);
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
        rcp_mock_server_dispatch(srv, 3, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
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

static void test_dispatch_no_handler_leaves_response_zeroed(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0x01};

    to_rcp_configured(srv); /* see to_rcp_configured()'s own comment */
    rcp_mock_server_add_endpoint(srv, 4, 1, true, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
        rcp_mock_server_dispatch(srv, 4, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
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
        rcp_mock_server_dispatch(srv, 5, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
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
    rcp_mock_server_dispatch(srv, 6, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
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
                                  RCP_ACF_MSG_TYPE_ABB, true, req, sizeof(req), &resp));
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

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, combined,
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

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, frame.data,
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

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, garbage,
                                                 sizeof(garbage), results, RCP_MOCK_MAX_FRAME_MEMBERS);
    TEST_ASSERT_EQUAL_UINT(0, dispatched);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_frame_reports_unknown_bus_for_undecodable_member(void)
{
    /* A syntactically well-formed member (rcp_sched_split_frame_members()
     * accepts it) whose byte_bus_id[10:8] bits are nonzero cannot decode
     * through rcp_acf_decode_abb() (RCP_ACF_ERR_BUS_ID_OVERFLOW, acf.h) --
     * this must surface as RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS, not a crash
     * or a bogus dispatch. */
    rcp_mock_server_t *srv = rcp_mock_server_new();
    uint8_t             raw[RCP_ACF_ABB_HEADER_LEN];
    rcp_mock_frame_member_result_t results[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t              dispatched;

    to_hw_configured(srv);

    memset(raw, 0, sizeof(raw));
    raw[0] = (uint8_t)(RCP_ACF_MSG_TYPE_ABB << 1) | 0x00u; /* type=ABB, len MSB=0 */
    raw[1] = (uint8_t)(RCP_ACF_ABB_HEADER_LEN / 4u);        /* len=2 quadlets, no payload */
    raw[2] = 0x01u; /* busid[10:8] = 001 -> overflow */

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, raw,
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

    dispatched = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, combined,
                                                 combined_len, results, 1);

    TEST_ASSERT_EQUAL_UINT(1, dispatched); /* only out_cap's worth, not both */
    TEST_ASSERT_EQUAL_UINT8(30, results[0].byte_bus_id);

    rcp_bytes_free(&frame1);
    rcp_bytes_free(&frame2);
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
    RUN_TEST(test_remove_endpoint_decrements_svr_ep_count);
    RUN_TEST(test_remove_endpoint_unknown_bus_returns_false);
    RUN_TEST(test_readd_after_remove_succeeds);
    RUN_TEST(test_set_endpoint_enable_unknown_bus_returns_false);
    RUN_TEST(test_queue_len_unknown_bus_is_zero);

    RUN_TEST(test_dispatch_dropped_by_lifecycle);
    RUN_TEST(test_dispatch_rejected_by_lifecycle_sends_request_rejected_error);
    RUN_TEST(test_pwrmode_resume_reenables_all_endpoints);
    RUN_TEST(test_pwrmode_resume_returns_false_before_handshake_echoed);
    RUN_TEST(test_dispatch_unknown_bus_after_lifecycle_accepts);
    RUN_TEST(test_dispatch_unknown_bus_sends_ep_not_found_error);
    RUN_TEST(test_dispatch_ok_runs_handler_immediately);
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

    RUN_TEST(test_strerror_never_null);

    return UNITY_END();
}
