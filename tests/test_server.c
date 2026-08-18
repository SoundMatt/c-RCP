/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-SRV-001
//cfusa:test REQ-SRV-002
//cfusa:test REQ-SRV-003
//cfusa:test REQ-SRV-023
#include "unity.h"

#include <rcp/rcp.h>
#include <rcp/server.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

static void test_disabled_endpoint_queues_submitted_requests(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1, 2, 3 };
    bool executed_now;

    rcp_server_endpoint_init(&ep, false);

    executed_now = rcp_server_endpoint_submit(&ep, body, sizeof(body), NULL);

    TEST_ASSERT_FALSE(executed_now);
    TEST_ASSERT_EQUAL_UINT(1, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_enabled_endpoint_reports_immediate_execution(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 9 };
    bool executed_now;

    rcp_server_endpoint_init(&ep, true);

    executed_now = rcp_server_endpoint_submit(&ep, body, sizeof(body), NULL);

    TEST_ASSERT_TRUE(executed_now);
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

/* REQ-SRV-003 (FIFO drain once re-enabled) and REQ-SRV-023 (refuses while
 * disabled) were split from one bundled requirement by the c-RCP-18
 * requirement-atomicity audit (issue #533): a bug that let drain_one()
 * dequeue while disabled would fail only the test below, while a bug that
 * broke FIFO ordering or the empty-queue sentinel once re-enabled would
 * fail only test_reenable_drains_queue_in_fifo_order() -- distinct failure
 * modes, each with its own id and its own assertion. */
//cfusa:test REQ-SRV-023
static void test_drain_one_refuses_while_disabled(void)
{
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1 };
    rcp_bytes_t out = {0};

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body), NULL);

    TEST_ASSERT_FALSE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT(1, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

//cfusa:test REQ-SRV-003
static void test_reenable_drains_queue_in_fifo_order(void)
{
    rcp_server_endpoint_t ep;
    uint8_t first[]  = { 0xAA };
    uint8_t second[] = { 0xBB };
    uint8_t third[]  = { 0xCC };
    rcp_bytes_t out = {0};

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, first, sizeof(first), NULL);
    (void)rcp_server_endpoint_submit(&ep, second, sizeof(second), NULL);
    (void)rcp_server_endpoint_submit(&ep, third, sizeof(third), NULL);
    TEST_ASSERT_EQUAL_UINT(3, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_set_enable(&ep, true);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xBB, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_TRUE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT8(0xCC, out.data[0]);
    rcp_bytes_free(&out);

    TEST_ASSERT_FALSE(rcp_server_endpoint_drain_one(&ep, &out));
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
}

static void test_endpoint_destroy_frees_a_nonempty_queue(void)
{
    /* No crash / no leak (checked under ASan/valgrind in CI) when an
     * endpoint is destroyed with requests still queued. */
    rcp_server_endpoint_t ep;
    uint8_t body[] = { 1, 2 };

    rcp_server_endpoint_init(&ep, false);
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body), NULL);
    (void)rcp_server_endpoint_submit(&ep, body, sizeof(body), NULL);

    rcp_server_endpoint_destroy(&ep);
    TEST_ASSERT_EQUAL_UINT(0, rcp_server_endpoint_queue_len(&ep));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_disabled_endpoint_queues_submitted_requests);
    RUN_TEST(test_enabled_endpoint_reports_immediate_execution);
    RUN_TEST(test_drain_one_refuses_while_disabled);
    RUN_TEST(test_reenable_drains_queue_in_fifo_order);
    RUN_TEST(test_endpoint_destroy_frees_a_nonempty_queue);

    return UNITY_END();
}
