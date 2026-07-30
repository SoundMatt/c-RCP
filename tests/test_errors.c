/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-WIREERR-001
//cfusa:test REQ-WIREERR-002
#include "unity.h"

#include <rcp/errors.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── numbering matches the governing spec's own table ─────────────────────── */

static void test_error_code_values_match_the_spec_table(void)
{
    TEST_ASSERT_EQUAL_INT(1,  RCP_ERROR_UNSUPPORTED_CMD);
    TEST_ASSERT_EQUAL_INT(2,  RCP_ERROR_SEQUENCER_NOT_KNOWN);
    TEST_ASSERT_EQUAL_INT(3,  RCP_ERROR_UNAUTHORIZED_ACCESS);
    TEST_ASSERT_EQUAL_INT(4,  RCP_ERROR_LOCKED_MEM_ACCESS);
    TEST_ASSERT_EQUAL_INT(5,  RCP_ERROR_REQUEST_CANCELED);
    TEST_ASSERT_EQUAL_INT(6,  RCP_ERROR_REQUEST_NOT_FOUND);
    TEST_ASSERT_EQUAL_INT(7,  RCP_ERROR_EP_ERROR);
    TEST_ASSERT_EQUAL_INT(8,  RCP_ERROR_EP_NOT_FOUND);
    TEST_ASSERT_EQUAL_INT(9,  RCP_ERROR_PWM_IN_NO_SIGNAL);
    TEST_ASSERT_EQUAL_INT(10, RCP_ERROR_REQUEST_STORAGE_OVERFLOW);
    TEST_ASSERT_EQUAL_INT(11, RCP_ERROR_REQUEST_REJECTED);
    TEST_ASSERT_EQUAL_INT(12, RCP_ERROR_POCI_FAILURE);
    TEST_ASSERT_EQUAL_INT(13, RCP_ERROR_PRESENTATION_TIME_TOO_FAR);
    TEST_ASSERT_EQUAL_INT(14, RCP_ERROR_GPTP_FAIL);
    TEST_ASSERT_EQUAL_INT(15, RCP_ERROR_INVALID_PARAMETER);
    TEST_ASSERT_EQUAL_INT(16, RCP_ERROR_CHAIN_ABORTED);
    TEST_ASSERT_EQUAL_INT(17, RCP_ERROR_CHAIN_ERROR);
}

static void test_error_none_is_zero_and_not_in_the_spec_table(void)
{
    /* This implementation's own addition, not a spec-assigned value --
     * pinned at 0 so it can never collide with a real 1-17 code. */
    TEST_ASSERT_EQUAL_INT(0, RCP_ERROR_NONE);
}

/* ── rcp_wire_error_string ─────────────────────────────────────────────────── */

static void test_wire_error_string_unique_nonempty(void)
{
    const rcp_wire_error_t codes[] = {
        RCP_ERROR_NONE, RCP_ERROR_UNSUPPORTED_CMD, RCP_ERROR_SEQUENCER_NOT_KNOWN,
        RCP_ERROR_UNAUTHORIZED_ACCESS, RCP_ERROR_LOCKED_MEM_ACCESS,
        RCP_ERROR_REQUEST_CANCELED, RCP_ERROR_REQUEST_NOT_FOUND, RCP_ERROR_EP_ERROR,
        RCP_ERROR_EP_NOT_FOUND, RCP_ERROR_PWM_IN_NO_SIGNAL,
        RCP_ERROR_REQUEST_STORAGE_OVERFLOW, RCP_ERROR_REQUEST_REJECTED,
        RCP_ERROR_POCI_FAILURE, RCP_ERROR_PRESENTATION_TIME_TOO_FAR,
        RCP_ERROR_GPTP_FAIL, RCP_ERROR_INVALID_PARAMETER, RCP_ERROR_CHAIN_ABORTED,
        RCP_ERROR_CHAIN_ERROR,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_wire_error_string(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_wire_error_string(codes[j])) != 0);
        }
    }
}

static void test_wire_error_string_unknown_value_not_null(void)
{
    const char *s = rcp_wire_error_string((rcp_wire_error_t)999);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(strlen(s) > 0);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_error_code_values_match_the_spec_table);
    RUN_TEST(test_error_none_is_zero_and_not_in_the_spec_table);

    RUN_TEST(test_wire_error_string_unique_nonempty);
    RUN_TEST(test_wire_error_string_unknown_value_not_null);

    return UNITY_END();
}
