/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-PWR-001
//cfusa:test REQ-PWR-002
//cfusa:test REQ-PWR-003
//cfusa:test REQ-PWR-004
//cfusa:test REQ-PWR-005
//cfusa:test REQ-PWR-006
//cfusa:test REQ-PWR-007
//cfusa:test REQ-PWR-008
//cfusa:test REQ-PWR-009
//cfusa:test REQ-PWR-010
//cfusa:test REQ-PWR-011
//cfusa:test REQ-PWR-012
//cfusa:test REQ-PWR-013
//cfusa:test REQ-PWR-014
//cfusa:test REQ-PWR-015
#include "unity.h"

#include <rcp/powerstate.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_avtp_addr_t make_addr(uint16_t unique_id, uint8_t byte_bus_id)
{
    static const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    rcp_avtp_addr_t addr;
    addr.stream_id   = rcp_stream_id_make(mac, unique_id);
    addr.byte_bus_id = byte_bus_id;
    return addr;
}

#define ADDR make_addr(1, 5)
#define OTHER_ADDR make_addr(2, 5)

/* ── strerror ──────────────────────────────────────────────────────────────── */

//cfusa:test REQ-PWR-010
static void test_strerror_never_null_and_distinct(void)
{
    const rcp_powerstate_errc_t codes[] = {
        RCP_POWERSTATE_OK, RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT, RCP_POWERSTATE_ERR_DECODE,
        RCP_POWERSTATE_ERR_UNEXPECTED_TXN, RCP_POWERSTATE_ERR_ENTRY_REFUSED, RCP_POWERSTATE_ERR_TRANSITION,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_powerstate_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_powerstate_strerror(codes[j])) != 0);
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_powerstate_strerror((rcp_powerstate_errc_t)999));
}

/* ── Manager creation / mode() ────────────────────────────────────────────── */

//cfusa:test REQ-PWR-011
//cfusa:test REQ-PWR-015
static void test_manager_constructs_with_zero_endpoints(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    TEST_ASSERT_NOT_NULL(m);
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-012
static void test_mode_starts_normal(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);

    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));

    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-012
static void test_mode_unknown_endpoint_is_normal(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));
    rcp_powerstate_manager_destroy(m);
}

/* ── encode_entry_request() / apply_entry_response() ─────────────────────── */

//cfusa:test REQ-PWR-001
static void test_encode_entry_request_unknown_endpoint_is_zeroed(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    rcp_bytes_t frame = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_STANDBY, 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL(0, frame.len);

    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-001
static void test_encode_entry_request_round_trips_via_ep_wakeup(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t frame;
    rcp_pwrmode_t out_mode;
    uint8_t out_txn;

    frame = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_SLEEP, 7);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                       rcp_ep_wakeup_decode_sleepcmd_request(frame.data, frame.len, ADDR.byte_bus_id,
                                                              &out_mode, &out_txn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, out_mode);
    TEST_ASSERT_EQUAL(7, out_txn);

    rcp_bytes_free(&frame);
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-002
static void test_apply_entry_response_ok_transitions_mode(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t req, resp;

    req = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_STANDBY, 3);
    TEST_ASSERT_NOT_NULL(req.data);
    rcp_bytes_free(&req);

    resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 3);
    TEST_ASSERT_NOT_NULL(resp.data);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_STANDBY, rcp_powerstate_manager_mode(m, ADDR));

    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-003
static void test_apply_entry_response_refused_leaves_mode_unchanged(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t req, resp;

    req = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_SLEEP, 4);
    rcp_bytes_free(&req);

    resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_REFUSED, 4);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_ENTRY_REFUSED,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));

    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-004
static void test_apply_entry_response_wrong_txn_rejected(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t req, resp;

    req = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_STANDBY, 5);
    rcp_bytes_free(&req);

    resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 6 /* wrong */);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_UNEXPECTED_TXN,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));

    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-004
static void test_apply_entry_response_no_pending_request_rejected(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 0);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_UNEXPECTED_TXN,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len));

    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

static void test_apply_entry_response_unknown_endpoint(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    rcp_bytes_t resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 0);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len));

    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

static void test_apply_entry_response_decode_failure(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    uint8_t garbage[] = {0x00};

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_DECODE,
                       rcp_powerstate_manager_apply_entry_response(m, ADDR, garbage, sizeof(garbage)));

    rcp_powerstate_manager_destroy(m);
}

/* ── wake_via_network() ───────────────────────────────────────────────────── */

static void put_to_sleep(rcp_powerstate_manager_t *m, rcp_avtp_addr_t addr, uint8_t txn)
{
    rcp_bytes_t req, resp;
    req = rcp_powerstate_manager_encode_entry_request(m, addr, RCP_PWRMODE_SLEEP, txn);
    rcp_bytes_free(&req);
    resp = rcp_ep_wakeup_encode_sleepcmd_response(addr.byte_bus_id, RCP_PWRMODE_ENTRY_OK, txn);
    rcp_powerstate_manager_apply_entry_response(m, addr, resp.data, resp.len);
    rcp_bytes_free(&resp);
}

//cfusa:test REQ-PWR-005
static void test_wake_via_network_from_sleep_is_hot(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_pwrmode_start_kind_t kind;

    put_to_sleep(m, ADDR, 1);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, rcp_powerstate_manager_mode(m, ADDR));

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK, rcp_powerstate_manager_wake_via_network(m, ADDR, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);

    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-005
static void test_wake_via_network_requires_sleep(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_TRANSITION, rcp_powerstate_manager_wake_via_network(m, ADDR, NULL));

    rcp_powerstate_manager_destroy(m);
}

static void test_wake_via_network_unknown_endpoint(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_UNKNOWN_ENDPOINT, rcp_powerstate_manager_wake_via_network(m, ADDR, NULL));
    rcp_powerstate_manager_destroy(m);
}

/* ── Pin-wake hot-start handshake ─────────────────────────────────────────── */

//cfusa:test REQ-PWR-006
//cfusa:test REQ-PWR-007
//cfusa:test REQ-PWR-008
//cfusa:test REQ-PWR-013
//cfusa:test REQ-PWR-014
static void test_wake_via_pin_hot_when_handshake_complete(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t probe, echo;
    rcp_pwrmode_start_kind_t kind;

    put_to_sleep(m, ADDR, 1);

    TEST_ASSERT_TRUE(rcp_powerstate_manager_handshake_begin(m, ADDR, 3, true));

    probe = rcp_powerstate_manager_encode_wakeup_probe(m, ADDR, 9);
    TEST_ASSERT_NOT_NULL(probe.data);
    /* Stand-in for the echo the wire would return -- this module owns no
     * transport of its own (see powerstate.h's file header). */
    echo = rcp_ep_wakeup_encode_wakeup_message(ADDR.byte_bus_id, 9);
    TEST_ASSERT_TRUE(rcp_powerstate_manager_apply_wakeup_echo(m, ADDR, echo.data, echo.len, 9));
    rcp_bytes_free(&probe);
    rcp_bytes_free(&echo);

    TEST_ASSERT_TRUE(rcp_powerstate_manager_handshake_resume_queues(m, ADDR));

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK, rcp_powerstate_manager_wake_via_pin(m, ADDR, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, ADDR));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);

    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-008
static void test_wake_via_pin_cold_when_handshake_not_started(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_pwrmode_start_kind_t kind;

    put_to_sleep(m, ADDR, 1);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK, rcp_powerstate_manager_wake_via_pin(m, ADDR, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);

    rcp_powerstate_manager_destroy(m);
}

static void test_wake_via_pin_requires_sleep(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);

    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_TRANSITION, rcp_powerstate_manager_wake_via_pin(m, ADDR, NULL));

    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-007
static void test_apply_wakeup_echo_wrong_txn_not_echoed(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t echo;

    TEST_ASSERT_TRUE(rcp_powerstate_manager_handshake_begin(m, ADDR, 1, true));

    echo = rcp_ep_wakeup_encode_wakeup_message(ADDR.byte_bus_id, 1);
    /* sent_transaction_num (2) doesn't match the echo's own txn (1): not
     * recognized as an echo, so this attempt is reported as "not echoed"
     * -- with a repeat_limit of 1, this exhausts it and fails. */
    TEST_ASSERT_FALSE(rcp_powerstate_manager_apply_wakeup_echo(m, ADDR, echo.data, echo.len, 2));

    rcp_bytes_free(&echo);
    rcp_powerstate_manager_destroy(m);
}

static void test_handshake_begin_unknown_endpoint(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    TEST_ASSERT_FALSE(rcp_powerstate_manager_handshake_begin(m, ADDR, 3, true));
    rcp_powerstate_manager_destroy(m);
}

//cfusa:test REQ-PWR-013
static void test_encode_wakeup_probe_unknown_endpoint_is_zeroed(void)
{
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(NULL, 0);
    rcp_bytes_t probe = rcp_powerstate_manager_encode_wakeup_probe(m, ADDR, 1);
    TEST_ASSERT_NULL(probe.data);
    rcp_powerstate_manager_destroy(m);
}

/* ── addr scoping ──────────────────────────────────────────────────────────── */

static void test_endpoints_are_independently_tracked(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR, OTHER_ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 2);

    put_to_sleep(m, ADDR, 1);

    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, rcp_powerstate_manager_mode(m, ADDR));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, OTHER_ADDR));

    rcp_powerstate_manager_destroy(m);
}

/* ── Subscription ─────────────────────────────────────────────────────────── */

static int g_event_count;
static rcp_powerstate_errc_t g_last_err;

static void count_events(const rcp_power_event_t *ev, void *user_data)
{
    (void)user_data;
    g_event_count++;
    g_last_err = ev->err;
}

//cfusa:test REQ-PWR-009
static void test_subscribe_fires_on_every_attempted_transition(void)
{
    rcp_avtp_addr_t endpoints[] = {ADDR};
    rcp_powerstate_manager_t *m = rcp_powerstate_manager_new(endpoints, 1);
    rcp_bytes_t req, resp;

    g_event_count = 0;
    TEST_ASSERT_TRUE(rcp_powerstate_manager_subscribe(m, count_events, NULL));

    put_to_sleep(m, ADDR, 1);
    TEST_ASSERT_EQUAL(1, g_event_count);
    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK, g_last_err);

    /* A refused entry request still fires an event, with a non-OK err. */
    req = rcp_powerstate_manager_encode_entry_request(m, ADDR, RCP_PWRMODE_STANDBY, 2);
    rcp_bytes_free(&req);
    resp = rcp_ep_wakeup_encode_sleepcmd_response(ADDR.byte_bus_id, RCP_PWRMODE_ENTRY_REFUSED, 2);
    rcp_powerstate_manager_apply_entry_response(m, ADDR, resp.data, resp.len);
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(2, g_event_count);
    TEST_ASSERT_EQUAL(RCP_POWERSTATE_ERR_ENTRY_REFUSED, g_last_err);

    rcp_powerstate_manager_destroy(m);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);
    RUN_TEST(test_manager_constructs_with_zero_endpoints);
    RUN_TEST(test_mode_starts_normal);
    RUN_TEST(test_mode_unknown_endpoint_is_normal);
    RUN_TEST(test_encode_entry_request_unknown_endpoint_is_zeroed);
    RUN_TEST(test_encode_entry_request_round_trips_via_ep_wakeup);
    RUN_TEST(test_apply_entry_response_ok_transitions_mode);
    RUN_TEST(test_apply_entry_response_refused_leaves_mode_unchanged);
    RUN_TEST(test_apply_entry_response_wrong_txn_rejected);
    RUN_TEST(test_apply_entry_response_no_pending_request_rejected);
    RUN_TEST(test_apply_entry_response_unknown_endpoint);
    RUN_TEST(test_apply_entry_response_decode_failure);
    RUN_TEST(test_wake_via_network_from_sleep_is_hot);
    RUN_TEST(test_wake_via_network_requires_sleep);
    RUN_TEST(test_wake_via_network_unknown_endpoint);
    RUN_TEST(test_wake_via_pin_hot_when_handshake_complete);
    RUN_TEST(test_wake_via_pin_cold_when_handshake_not_started);
    RUN_TEST(test_wake_via_pin_requires_sleep);
    RUN_TEST(test_apply_wakeup_echo_wrong_txn_not_echoed);
    RUN_TEST(test_handshake_begin_unknown_endpoint);
    RUN_TEST(test_encode_wakeup_probe_unknown_endpoint_is_zeroed);
    RUN_TEST(test_endpoints_are_independently_tracked);
    RUN_TEST(test_subscribe_fires_on_every_attempted_transition);

    return UNITY_END();
}
