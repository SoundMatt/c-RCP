//cfusa:test REQ-LOAN-001
//cfusa:test REQ-LOAN-002
//cfusa:test REQ-LOAN-003
//cfusa:test REQ-LOAN-004
//cfusa:test REQ-LOAN-005
//cfusa:test REQ-LOAN-006
//cfusa:test REQ-LOAN-007
//cfusa:test REQ-LOAN-008
#include "unity.h"

#include <rcp/loan.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── loan() ────────────────────────────────────────────────────────────────── */

static void test_loan_returns_zeroed_buffer_of_requested_size(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_loan_t *loan = NULL;
    size_t i;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_loan(lc, 16, &loan));
    TEST_ASSERT_NOT_NULL(loan);
    TEST_ASSERT_EQUAL_UINT(16, loan->payload.len);
    for (i = 0; i < loan->payload.len; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, loan->payload.data[i]);
    }

    rcp_loan_release(loan);
    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

static void test_loan_returns_closed_when_closed(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_loan_t *loan = NULL;

    rcp_controller_close(lc);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_loan(lc, 8, &loan));

    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

static void test_loan_errors_for_negative_size(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_loan_t *loan = NULL;

    /* Any error is acceptable for invalid size, per cpp-RCP's own contract. */
    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_controller_loan(lc, -1, &loan));

    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

/* ── send_loaned() ─────────────────────────────────────────────────────────── */

static void test_send_loaned_delivers_command_and_returns_ok(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_loan_t *loan = NULL;
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    rcp_context_t ctx = rcp_context_background();
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_loan(lc, 4, &loan));
    memcpy(loan->payload.data, payload, sizeof(payload));

    cmd.zone         = RCP_ZONE_FRONT_LEFT;
    cmd.payload.data = loan->payload.data;
    cmd.payload.len  = loan->payload.len;
    rcp_loan_return(loan); /* release back to pool (simulate send_loaned transfer) */

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send_loaned(lc, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_loan_release(loan);
    rcp_response_free(&resp);
    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

/* ── Pool return ───────────────────────────────────────────────────────────── */

static void test_loan_release_returns_buffer_to_pool(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_loan_t *loan1 = NULL;
    rcp_loan_t *loan2 = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_loan(lc, 8, &loan1));
    rcp_loan_release(loan1); /* returns to pool without crash */

    /* Loan again to verify the pool is reusable. */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_loan(lc, 8, &loan2));
    TEST_ASSERT_NOT_NULL(loan2);

    rcp_loan_release(loan2);
    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

/* ── Zone passthrough ──────────────────────────────────────────────────────── */

static void test_zone_returns_inner_zone(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_RIGHT, rcp_controller_zone(lc));

    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

static void test_send_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.id   = 7;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(lc, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL_UINT32(7, resp.command_id);
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, resp.status);

    rcp_response_free(&resp);
    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = make_mock(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *lc = rcp_loan_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(lc, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(lc);
    rcp_controller_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_loan_returns_zeroed_buffer_of_requested_size);
    RUN_TEST(test_loan_returns_closed_when_closed);
    RUN_TEST(test_loan_errors_for_negative_size);
    RUN_TEST(test_send_loaned_delivers_command_and_returns_ok);
    RUN_TEST(test_loan_release_returns_buffer_to_pool);
    RUN_TEST(test_zone_returns_inner_zone);
    RUN_TEST(test_send_delegates_to_inner);
    RUN_TEST(test_subscribe_delegates_to_inner);

    return UNITY_END();
}
