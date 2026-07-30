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
/* Tests the TC18-shaped RC-Server/endpoint test double (ROADMAP.md
 * milestone 77). The pre-TC18 zone-controller mock this file used to test
 * moved to tests/legacy_mock.h/.c; tests/test_legacy_mock.c (a renamed
 * copy of this file's own old content) now tests that instead. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
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

static void to_hw_configured(rcp_mock_server_t *srv)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP));
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
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
        rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP));
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

static void test_dispatch_unknown_bus_after_lifecycle_accepts(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {0xAA};

    to_hw_configured(srv); /* any byte_bus_id passes lifecycle admission now */

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS,
        rcp_mock_server_dispatch(srv, 7, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true,
                                  req, sizeof(req), &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

static void test_dispatch_ok_runs_handler_immediately(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t resp = {0};
    const uint8_t req[] = {1, 2, 3};
    int user_data_marker = 7;

    to_hw_configured(srv);
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

    to_hw_configured(srv);
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

    to_hw_configured(srv);
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

    to_hw_configured(srv);
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
    RUN_TEST(test_dispatch_unknown_bus_after_lifecycle_accepts);
    RUN_TEST(test_dispatch_ok_runs_handler_immediately);
    RUN_TEST(test_dispatch_no_handler_leaves_response_zeroed);
    RUN_TEST(test_dispatch_queued_when_endpoint_disabled);
    RUN_TEST(test_drain_endpoint_runs_queued_request);
    RUN_TEST(test_drain_endpoint_empty_queue_returns_false);
    RUN_TEST(test_drain_endpoint_unknown_bus_returns_false);
    RUN_TEST(test_discovery_bus_accepted_while_hw_unconfigured);

    RUN_TEST(test_strerror_never_null);

    return UNITY_END();
}
