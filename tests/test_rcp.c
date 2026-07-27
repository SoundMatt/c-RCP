//cfusa:test REQ-ZONE-001
//cfusa:test REQ-ZONE-002
//cfusa:test REQ-ZONE-003
//cfusa:test REQ-ZONE-004
//cfusa:test REQ-ZONE-005
//cfusa:test REQ-ZONE-006
//cfusa:test REQ-ZONE-007
//cfusa:test REQ-ZONE-008
//cfusa:test REQ-PRI-001
//cfusa:test REQ-PRI-002
//cfusa:test REQ-PRI-003
//cfusa:test REQ-CMD-001
//cfusa:test REQ-CMD-002
//cfusa:test REQ-CMD-003
//cfusa:test REQ-CMD-004
//cfusa:test REQ-CMD-005
//cfusa:test REQ-CMD-006
//cfusa:test REQ-STATUS-001
//cfusa:test REQ-STATUS-002
//cfusa:test REQ-STATUS-003
//cfusa:test REQ-STATUS-004
//cfusa:test REQ-STATUS-005
//cfusa:test REQ-STATUS-006
//cfusa:test REQ-ERR-001
//cfusa:test REQ-ERR-002
//cfusa:test REQ-ERR-003
//cfusa:test REQ-ERR-004
//cfusa:test REQ-ERR-005
//cfusa:test REQ-ERR-006
//cfusa:test REQ-ERR-007
//cfusa:test REQ-ERR-008
//cfusa:test REQ-ERR-009
//cfusa:test REQ-ERR-010
//cfusa:test REQ-ERR-011
//cfusa:test REQ-CMDSTRUCT-001
//cfusa:test REQ-CMDSTRUCT-002
//cfusa:test REQ-RESP-003
//cfusa:test REQ-STAT-005
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Portable short busy-wait for the context-expiry tests below — avoids
 * pulling any OS sleep API into test code beyond the library's own public
 * rcp_monotonic_ms(). */
static void spin_wait_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

/* ── Zone constants ────────────────────────────────────────────────────────── */

static void test_zone_string_unique_nonempty(void)
{
    const rcp_zone_t zones[] = {
        RCP_ZONE_FRONT_LEFT, RCP_ZONE_FRONT_RIGHT,
        RCP_ZONE_REAR_LEFT,  RCP_ZONE_REAR_RIGHT,
        RCP_ZONE_CENTRAL,
    };
    const size_t n = sizeof(zones) / sizeof(zones[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_zone_string(zones[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(s, rcp_zone_string(zones[j])) != 0 ? 1 : 0);
        }
    }
}

static void test_zone_values(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, RCP_ZONE_UNKNOWN);
    TEST_ASSERT_EQUAL_UINT8(1, RCP_ZONE_FRONT_LEFT);
    TEST_ASSERT_EQUAL_UINT8(2, RCP_ZONE_FRONT_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(3, RCP_ZONE_REAR_LEFT);
    TEST_ASSERT_EQUAL_UINT8(4, RCP_ZONE_REAR_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(5, RCP_ZONE_CENTRAL);
}

static void test_zone_values_distinct(void)
{
    const rcp_zone_t zones[] = {
        RCP_ZONE_FRONT_LEFT, RCP_ZONE_FRONT_RIGHT,
        RCP_ZONE_REAR_LEFT,  RCP_ZONE_REAR_RIGHT,
        RCP_ZONE_CENTRAL,
    };
    const size_t n = sizeof(zones) / sizeof(zones[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) TEST_ASSERT_NOT_EQUAL(zones[i], zones[j]);
        }
    }
}

/* ── Priority constants ────────────────────────────────────────────────────── */

static void test_priority_ordering(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, RCP_PRIORITY_NORMAL);
    TEST_ASSERT_TRUE(RCP_PRIORITY_HIGH > RCP_PRIORITY_NORMAL);
    TEST_ASSERT_TRUE(RCP_PRIORITY_CRITICAL > RCP_PRIORITY_HIGH);
}

/* ── CommandType constants ─────────────────────────────────────────────────── */

static void test_command_type_values(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, RCP_CMD_NOOP);
    TEST_ASSERT_EQUAL_UINT16(1, RCP_CMD_SET);
    TEST_ASSERT_EQUAL_UINT16(2, RCP_CMD_GET);
    TEST_ASSERT_EQUAL_UINT16(3, RCP_CMD_RESET);
    TEST_ASSERT_EQUAL_UINT16(4, RCP_CMD_WATCHDOG);
    TEST_ASSERT_EQUAL_UINT16(5, RCP_CMD_SLEEP);
    TEST_ASSERT_EQUAL_UINT16(6, RCP_CMD_WAKE);
}

static void test_command_type_distinct(void)
{
    const rcp_command_type_t cmds[] = {
        RCP_CMD_NOOP, RCP_CMD_SET, RCP_CMD_GET, RCP_CMD_RESET, RCP_CMD_WATCHDOG,
    };
    const size_t n = sizeof(cmds) / sizeof(cmds[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) TEST_ASSERT_NOT_EQUAL(cmds[i], cmds[j]);
        }
    }
}

/* ── ResponseStatus constants ──────────────────────────────────────────────── */

static void test_response_status_string_unique_nonempty(void)
{
    const rcp_response_status_t statuses[] = {
        RCP_RESPONSE_OK, RCP_RESPONSE_ERROR, RCP_RESPONSE_TIMEOUT,
        RCP_RESPONSE_BUSY, RCP_RESPONSE_UNKNOWN,
    };
    const size_t n = sizeof(statuses) / sizeof(statuses[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        const char *s = rcp_response_status_string(statuses[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_response_status_string(statuses[j])) != 0);
        }
    }
}

static void test_response_status_values(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, RCP_RESPONSE_OK);
    TEST_ASSERT_EQUAL_UINT8(1, RCP_RESPONSE_ERROR);
    TEST_ASSERT_EQUAL_UINT8(2, RCP_RESPONSE_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT8(3, RCP_RESPONSE_BUSY);
}

static void test_response_status_distinct(void)
{
    const rcp_response_status_t statuses[] = {
        RCP_RESPONSE_OK, RCP_RESPONSE_ERROR, RCP_RESPONSE_TIMEOUT,
        RCP_RESPONSE_BUSY, RCP_RESPONSE_UNKNOWN,
    };
    const size_t n = sizeof(statuses) / sizeof(statuses[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) TEST_ASSERT_NOT_EQUAL(statuses[i], statuses[j]);
        }
    }
}

/* ── Sentinel error codes ──────────────────────────────────────────────────── */

static void test_sentinel_errors_nonzero(void)
{
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_CLOSED);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_ALREADY_EXISTS);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_TIMEOUT);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_BUSY);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_ZONE_MISMATCH);
}

static void test_sentinel_errors_distinct(void)
{
    const rcp_errc_t sentinels[] = {
        RCP_ERR_CLOSED, RCP_ERR_NOT_FOUND, RCP_ERR_ALREADY_EXISTS,
        RCP_ERR_TIMEOUT, RCP_ERR_BUSY, RCP_ERR_ZONE_MISMATCH,
    };
    const size_t n = sizeof(sentinels) / sizeof(sentinels[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) TEST_ASSERT_NOT_EQUAL(sentinels[i], sentinels[j]);
        }
    }
}

static void test_zone_mismatch_distinct_from_others(void)
{
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_ZONE_MISMATCH, RCP_ERR_CLOSED);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_ZONE_MISMATCH, RCP_ERR_NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_ZONE_MISMATCH, RCP_ERR_ALREADY_EXISTS);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_ZONE_MISMATCH, RCP_ERR_TIMEOUT);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_ZONE_MISMATCH, RCP_ERR_BUSY);
}

/* ── Struct zero values ────────────────────────────────────────────────────── */

static void test_zero_command_is_safe(void)
{
    rcp_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    TEST_ASSERT_EQUAL(RCP_ZONE_UNKNOWN, cmd.zone);
    TEST_ASSERT_EQUAL(RCP_CMD_NOOP, cmd.type);
    TEST_ASSERT_EQUAL(RCP_PRIORITY_NORMAL, cmd.priority);
    TEST_ASSERT_NULL(cmd.payload.data);
    TEST_ASSERT_EQUAL_UINT(0, cmd.payload.len);
}

static void test_zero_response_has_ok_status(void)
{
    rcp_response_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_EQUAL(RCP_RESPONSE_OK, r.status);
}

static void test_zero_status_has_empty_payload(void)
{
    rcp_status_t s;
    memset(&s, 0, sizeof(s));
    TEST_ASSERT_NULL(s.payload.data);
    TEST_ASSERT_EQUAL_UINT(0, s.payload.len);
}

/* ── Context ───────────────────────────────────────────────────────────────── */

static void test_context_background_never_done(void)
{
    rcp_context_t ctx = rcp_context_background();
    TEST_ASSERT_FALSE(rcp_context_done(&ctx));
}

static void test_context_with_timeout_expires(void)
{
    rcp_context_t ctx = rcp_context_with_timeout_ms(1);
    spin_wait_ms(5);
    TEST_ASSERT_TRUE(rcp_context_done(&ctx));
}

static void test_context_with_past_deadline_immediately_done(void)
{
    rcp_context_t ctx = rcp_context_with_deadline_ms(rcp_monotonic_ms() - 1000);
    TEST_ASSERT_TRUE(rcp_context_done(&ctx));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_zone_string_unique_nonempty);
    RUN_TEST(test_zone_values);
    RUN_TEST(test_zone_values_distinct);

    RUN_TEST(test_priority_ordering);

    RUN_TEST(test_command_type_values);
    RUN_TEST(test_command_type_distinct);

    RUN_TEST(test_response_status_string_unique_nonempty);
    RUN_TEST(test_response_status_values);
    RUN_TEST(test_response_status_distinct);

    RUN_TEST(test_sentinel_errors_nonzero);
    RUN_TEST(test_sentinel_errors_distinct);
    RUN_TEST(test_zone_mismatch_distinct_from_others);

    RUN_TEST(test_zero_command_is_safe);
    RUN_TEST(test_zero_response_has_ok_status);
    RUN_TEST(test_zero_status_has_empty_payload);

    RUN_TEST(test_context_background_never_done);
    RUN_TEST(test_context_with_timeout_expires);
    RUN_TEST(test_context_with_past_deadline_immediately_done);

    return UNITY_END();
}
